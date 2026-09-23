/*
 * LuminaOS - Kernel Interactive Shell
 * Single-file implementation, entry point: void _start(void)
 *
 * Runs in ring0, directly calls kernel APIs:
 *   fat16_*   filesystem
 *   pmm_*     physical memory
 *   heap_*    kernel heap
 *   timer_*   ticks
 *   gfx_*     framebuffer (for `ui` / `run`)
 *   mouse_*   mouse
 *   lsp_load  LSP program loader
 */

#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/system.h>
#include <kernel/fat16.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/paging.h>
#include <kernel/timer.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/desktop.h>
#include <kernel/lsp.h>
#include <kernel/io.h>
#include <stdint.h>

/* ============================================================
 *  Constants
 * ============================================================ */

#define SHELL_LINE_MAX   128
#define SHELL_ARG_MAX    16
#define SHELL_HIST_MAX   16
#define SHELL_PATH_MAX   64
#define SHELL_USER_MAX   16

/* ============================================================
 *  Small string helpers (no libc in kernel)
 * ============================================================ */

static int sh_streq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int sh_strlen(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void sh_strcpy(char* dst, const char* src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static char sh_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int sh_ieq(const char* a, const char* b)
{
    while (*a && *b) {
        if (sh_lower(*a) != sh_lower(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void sh_print_uint(uint32_t v)
{
    char buf[10];
    int len = 0;
    if (v == 0) { tty_putchar('0'); return; }
    while (v > 0) { buf[len++] = (char)('0' + v % 10); v /= 10; }
    while (len > 0) tty_putchar(buf[--len]);
}

/* ============================================================
 *  Shell state
 * ============================================================ */

static int  sh_fs_ready;

static char sh_user[SHELL_USER_MAX];       /* "" = guest */

static char sh_cwd[SHELL_PATH_MAX];        /* display path, e.g. "/docs/bin" */

static char sh_hist[SHELL_HIST_MAX][SHELL_LINE_MAX];
static int  sh_hist_count;                 /* entries stored */
static int  sh_hist_head;                  /* next write slot */

/* ============================================================
 *  cwd bookkeeping
 *
 *  We keep a display path string in sync with fat16's cur_cluster.
 *  Every successful cd updates both.
 * ============================================================ */

static void sh_cwd_reset(void)
{
    sh_cwd[0] = '/';
    sh_cwd[1] = '\0';
}

static void sh_cwd_push(const char* name)
{
    int len = sh_strlen(sh_cwd);
    int nlen = sh_strlen(name);

    if (sh_streq(sh_cwd, "/")) {
        /* root: "/" + name */
        if (nlen >= SHELL_PATH_MAX - 1) return;
        sh_cwd[0] = '/';
        for (int i = 0; i < nlen; i++) sh_cwd[1 + i] = name[i];
        sh_cwd[1 + nlen] = '\0';
        return;
    }

    if (len + 1 + nlen >= SHELL_PATH_MAX) return;
    sh_cwd[len] = '/';
    for (int i = 0; i < nlen; i++) sh_cwd[len + 1 + i] = name[i];
    sh_cwd[len + 1 + nlen] = '\0';
}

static void sh_cwd_pop(void)
{
    int len = sh_strlen(sh_cwd);
    if (len <= 1) { sh_cwd_reset(); return; }

    int i = len - 1;
    while (i > 0 && sh_cwd[i] != '/') i--;
    if (i == 0) { sh_cwd_reset(); return; }

    sh_cwd[i] = '\0';
}

/* ============================================================
 *  Prompt
 * ============================================================ */

static void sh_prompt(void)
{
    tty_puts("LuminaOS:");
    tty_puts(sh_cwd);
    tty_puts(" [");
    tty_puts(sh_user[0] ? sh_user : "guest");
    tty_puts("]> ");
}

/* ============================================================
 *  Line editor (with history + basic editing)
 * ============================================================ */

static void sh_line_redraw(const char* buf, int len)
{
    /* naive: backspace over the printed chars, reprint */
    for (int i = 0; i < len; i++) {
        tty_putchar('\b');
        tty_putchar(' ');
        tty_putchar('\b');
    }
    for (int i = 0; i < len; i++)
        tty_putchar(buf[i]);
}

static int sh_read_line(char* out)
{
    int pos = 0;
    int hist_nav = sh_hist_count;   /* sh_hist_count = "off the end" = current */

    out[0] = '\0';

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            tty_putchar('\n');
            out[pos] = '\0';
            return pos;
        }

        if (c == '\b') {
            if (pos > 0) {
                pos--;
                tty_putchar('\b');
                tty_putchar(' ');
                tty_putchar('\b');
            }
            continue;
        }

        /* Up arrow: ESC [ A  -> 0x1B 0x5B 0x41 */
        if (c == 0x1B) {
            char a = keyboard_getchar();
            char b = keyboard_getchar();
            if (a == '[' && b == 'A') {
                if (sh_hist_count > 0 && hist_nav > 0) {
                    hist_nav--;
                    int idx = (sh_hist_head - sh_hist_count + hist_nav + SHELL_HIST_MAX * 2) % SHELL_HIST_MAX;
                    sh_strcpy(out, sh_hist[idx], SHELL_LINE_MAX);
                    int newlen = sh_strlen(out);
                    /* clear current line, print recalled */
                    for (int i = 0; i < pos; i++) { tty_putchar('\b'); tty_putchar(' '); tty_putchar('\b'); }
                    tty_puts(out);
                    pos = newlen;
                }
            } else if (a == '[' && b == 'B') {
                if (hist_nav < sh_hist_count) {
                    hist_nav++;
                    if (hist_nav == sh_hist_count) {
                        for (int i = 0; i < pos; i++) { tty_putchar('\b'); tty_putchar(' '); tty_putchar('\b'); }
                        pos = 0;
                        out[0] = '\0';
                    } else {
                        int idx = (sh_hist_head - sh_hist_count + hist_nav + SHELL_HIST_MAX * 2) % SHELL_HIST_MAX;
                        sh_strcpy(out, sh_hist[idx], SHELL_LINE_MAX);
                        int newlen = sh_strlen(out);
                        for (int i = 0; i < pos; i++) { tty_putchar('\b'); tty_putchar(' '); tty_putchar('\b'); }
                        tty_puts(out);
                        pos = newlen;
                    }
                }
            }
            continue;
        }

        if (c >= ' ' && c <= '~') {
            if (pos < SHELL_LINE_MAX - 1) {
                out[pos++] = c;
                tty_putchar(c);
            }
        }
    }
}

static void sh_hist_add(const char* line)
{
    if (!line[0]) return;
    /* skip consecutive duplicates */
    if (sh_hist_count > 0) {
        int last = (sh_hist_head - 1 + SHELL_HIST_MAX) % SHELL_HIST_MAX;
        if (sh_streq(sh_hist[last], line)) return;
    }
    sh_strcpy(sh_hist[sh_hist_head], line, SHELL_LINE_MAX);
    sh_hist_head = (sh_hist_head + 1) % SHELL_HIST_MAX;
    if (sh_hist_count < SHELL_HIST_MAX) sh_hist_count++;
}

/* ============================================================
 *  Command handlers
 * ============================================================ */

static void do_help(int argc, char** argv);

static void do_clear(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_clear();
    tty_puts("LuminaOS Shell - type 'help' for commands\n\n");
}

static void do_echo(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) tty_putchar(' ');
        tty_puts(argv[i]);
    }
    tty_putchar('\n');
}

static void do_ver(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts("LuminaOS v0.7.1\n");
    tty_puts("Architecture: i686 (32-bit)\n");
    tty_puts("Kernel: Hybrid\n");
    tty_puts("Filesystem: FAT16\n");
    tty_puts("CJK glyphs loaded: ");
    sh_print_uint((uint32_t)gfx_cjk_count());
    tty_putchar('\n');
}

static void do_reboot(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts("Rebooting...\n");
    system_reboot();
}

static void do_halt(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts("System halted.\n");
    system_halt();
}

static void do_mem(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts("Physical memory:\n");
    tty_puts("  Total:   "); sh_print_uint(PMM_MAX_PHYS / 1024);   tty_puts(" KB\n");
    tty_puts("  Free:    "); sh_print_uint((pmm_free_page_count() * PMM_PAGE_SIZE) / 1024); tty_puts(" KB\n");
    tty_puts("  Used:    "); sh_print_uint((pmm_used_page_count() * PMM_PAGE_SIZE) / 1024); tty_puts(" KB\n");
    tty_puts("Kernel heap:\n");
    tty_puts("  Free:    "); sh_print_uint(heap_free_bytes() / 1024); tty_puts(" KB\n");
    tty_puts("  Used:    "); sh_print_uint(heap_used_bytes() / 1024); tty_puts(" KB\n");
}

static void do_memtest(int argc, char** argv)
{
    (void)argc; (void)argv;

    char* a = (char*)kmalloc(100);
    char* b = (char*)kmalloc(64);
    char* c = (char*)kmalloc(4096);
    if (!a || !b || !c) { tty_puts("FAIL: alloc\n"); return; }
    if (((uintptr_t)a % 8) || ((uintptr_t)b % 8) || ((uintptr_t)c % 8)) {
        tty_puts("FAIL: align\n"); return;
    }

    for (int i = 0; i < 100; i++) a[i] = (char)i;
    for (int i = 0; i < 4096; i++) c[i] = (char)(i & 0xFF);
    for (int i = 0; i < 100; i++) if (a[i] != (char)i) { tty_puts("FAIL: write a\n"); return; }
    for (int i = 0; i < 4096; i++) if (c[i] != (char)(i & 0xFF)) { tty_puts("FAIL: write c\n"); return; }

    kfree(a);
    kfree(c);

    char* d = (char*)kmalloc(100);
    if (!d) { tty_puts("FAIL: reuse\n"); return; }
    kfree(d);
    kfree(b);

    uintptr_t p1 = pmm_alloc_zero();
    uintptr_t p2 = pmm_alloc_zero();
    if (!p1 || !p2) { tty_puts("FAIL: pmm alloc\n"); return; }
    if (p1 == p2 || p1 % PMM_PAGE_SIZE || p2 % PMM_PAGE_SIZE) {
        tty_puts("FAIL: pmm pages\n"); return;
    }
    for (int i = 0; i < PMM_PAGE_SIZE; i++)
        if (((char*)p1)[i] != 0) { tty_puts("FAIL: pmm zero\n"); return; }
    pmm_free(p1);
    if (pmm_alloc() != p1) { tty_puts("FAIL: pmm reuse\n"); return; }
    pmm_free(p1);
    pmm_free(p2);

    tty_puts("PASS\n");
}

static void do_uptime(int argc, char** argv)
{
    (void)argc; (void)argv;
    uint32_t ticks = timer_ticks();
    tty_puts("Uptime: ");
    sh_print_uint(ticks / 1000);
    tty_putchar('.');
    sh_print_uint((ticks % 1000) / 100);
    tty_puts("s\n");
}

/* ---- user management ---- */

static int sh_username_valid(const char* s)
{
    if (!*s) return 0;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    }
    return 1;
}

static int sh_read_users(char* buf, int max)
{
    uint32_t sz = 0;
    if (fat16_read_file_in(0, "USERS.DAT", buf, (uint32_t)max, &sz) != 0)
        return 0;
    buf[sz] = '\0';
    return (int)sz;
}

static int sh_user_exists(const char* data, const char* user)
{
    const char* p = data;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            int len = (int)(colon - line);
            if (sh_strlen(user) == len) {
                int match = 1;
                for (int i = 0; i < len; i++) {
                    if (sh_lower(line[i]) != sh_lower(user[i])) { match = 0; break; }
                }
                if (match) return 1;
            }
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static int sh_user_check(const char* data, const char* user, const char* pass)
{
    const char* p = data;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            int ulen = (int)(colon - line);
            const char* pp = colon + 1;
            int plen = (int)(p - pp);
            if (sh_strlen(user) == ulen && sh_strlen(pass) == plen) {
                int umatch = 1;
                for (int i = 0; i < ulen; i++) {
                    if (sh_lower(line[i]) != sh_lower(user[i])) { umatch = 0; break; }
                }
                int pmatch = 1;
                for (int i = 0; i < plen; i++) {
                    if (pp[i] != pass[i]) { pmatch = 0; break; }
                }
                if (umatch && pmatch) return 1;
            }
        }
        if (*p == '\n') p++;
    }
    return 0;
}

static void do_register(int argc, char** argv)
{
    if (argc < 3) { tty_puts("Usage: register <username> <password>\n"); return; }

    const char* user = argv[1];
    const char* pass = argv[2];

    if (!sh_username_valid(user) || !sh_username_valid(pass)) {
        tty_puts("Invalid username or password\n");
        return;
    }

    char buf[4096];
    int n = sh_read_users(buf, sizeof(buf) - 1);
    if (n > 0 && sh_user_exists(buf, user)) {
        tty_puts("User already exists\n");
        return;
    }

    int ulen = sh_strlen(user);
    int plen = sh_strlen(pass);
    if (n + ulen + 1 + plen + 1 >= (int)sizeof(buf)) {
        tty_puts("User database full\n");
        return;
    }

    for (int i = 0; i < ulen; i++) buf[n++] = sh_lower(user[i]);
    buf[n++] = ':';
    for (int i = 0; i < plen; i++) buf[n++] = pass[i];
    buf[n++] = '\n';

    fat16_set_write_attr(FAT16_ATTR_ARCHIVE | FAT16_ATTR_HIDDEN);
    if (fat16_write_file_in(0, "USERS.DAT", buf, (uint32_t)n) != 0) {
        tty_puts("Register failed\n");
        return;
    }

    char verify[4096];
    if (sh_read_users(verify, sizeof(verify) - 1) > 0 && sh_user_exists(verify, user)) {
        tty_puts("User registered. Use login to sign in\n");
    } else {
        tty_puts("Register failed (write did not persist)\n");
    }
}

static void do_login(int argc, char** argv)
{
    if (argc < 3) { tty_puts("Usage: login <username> <password>\n"); return; }

    const char* user = argv[1];
    const char* pass = argv[2];

    char buf[4096];
    int n = sh_read_users(buf, sizeof(buf) - 1);
    if (n == 0) {
        tty_puts("No users. Register first\n");
        return;
    }
    if (!sh_user_check(buf, user, pass)) {
        tty_puts("Login failed\n");
        return;
    }

    sh_strcpy(sh_user, user, SHELL_USER_MAX);
    tty_puts("Welcome, ");
    tty_puts(sh_user);
    tty_putchar('\n');
}

static void do_logout(int argc, char** argv)
{
    (void)argc; (void)argv;
    if (!sh_user[0]) {
        tty_puts("Not logged in\n");
        return;
    }
    sh_user[0] = '\0';
    while (!fat16_is_root())
        fat16_cd("..");
    sh_cwd_reset();
    tty_puts("Logged out\n");
}

static void do_users(int argc, char** argv)
{
    (void)argc; (void)argv;
    char buf[4096];
    int n = sh_read_users(buf, sizeof(buf) - 1);
    if (n == 0) { tty_puts("No users registered\n"); return; }

    const char* p = buf;
    while (*p) {
        const char* line = p;
        const char* colon = 0;
        while (*p && *p != '\n') {
            if (*p == ':' && !colon) colon = p;
            p++;
        }
        if (colon) {
            for (const char* q = line; q < colon; q++) tty_putchar(*q);
            tty_putchar('\n');
        }
        if (*p == '\n') p++;
    }
}

/* ---- filesystem ---- */

static void do_ls(int argc, char** argv)
{
    (void)argc; (void)argv;

    fat16_entry_t entries[64];
    int count = 0;
    if (fat16_list_dir(entries, 64, &count) != 0) {
        tty_puts("Failed to read directory\n");
        return;
    }
    if (count == 0) { tty_puts("(empty)\n"); return; }

    for (int i = 0; i < count; i++) {
        char name_buf[13];
        int len = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            name_buf[len++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') {
            name_buf[len++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++)
                name_buf[len++] = entries[i].name[j];
        }
        name_buf[len] = '\0';

        tty_puts("  ");
        tty_puts(name_buf);
        if (entries[i].attr & FAT16_ATTR_DIRECTORY) {
            tty_puts("/");
            uint32_t dir_size = 0;
            if (fat16_dir_total_size(entries[i].first_cluster, &dir_size) == 0) {
                tty_puts("  (");
                sh_print_uint(dir_size);
                tty_puts(" B)\n");
            } else {
                tty_puts("  (0 B)\n");
            }
        } else {
            tty_puts("  (");
            sh_print_uint(entries[i].size);
            tty_puts(" B)\n");
        }
    }
}

static void do_cd(int argc, char** argv)
{
    if (argc < 2) { tty_puts("Usage: cd <dirname>\n"); return; }

    if (sh_streq(argv[1], "..")) {
        if (fat16_is_root()) return;
        if (fat16_cd("..") == 0) sh_cwd_pop();
        else tty_puts("Not found\n");
        return;
    }

    if (sh_streq(argv[1], "/")) {
        while (!fat16_is_root()) fat16_cd("..");
        sh_cwd_reset();
        return;
    }

    if (fat16_cd(argv[1]) == 0)
        sh_cwd_push(argv[1]);
    else
        tty_puts("Not found\n");
}

static void do_mkdir(int argc, char** argv)
{
    if (argc < 2) { tty_puts("Usage: mkdir <dirname>\n"); return; }
    if (fat16_mkdir(argv[1]) != 0)
        tty_puts("Mkdir failed\n");
    else
        tty_puts("Created\n");
}

static void do_cat(int argc, char** argv)
{
    if (argc < 2) { tty_puts("Usage: cat <filename>\n"); return; }

    static uint8_t buf[8192];
    uint32_t size = 0;
    if (fat16_read_file(argv[1], buf, sizeof(buf), &size) != 0) {
        tty_puts("File not found\n");
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        char c = (char)buf[i];
        if (c >= 32 && c <= 126) tty_putchar(c);
        else if (c == '\n') tty_putchar('\n');
        else if (c == '\t') tty_putchar('\t');
        else if (c == '\r') continue;
        else tty_putchar('.');
    }
    tty_putchar('\n');
}

static void do_write(int argc, char** argv)
{
    if (argc < 3) { tty_puts("Usage: write <filename> <content...>\n"); return; }

    /* join argv[2..] with spaces */
    static char content[1024];
    int pos = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            if (pos < (int)sizeof(content) - 1) content[pos++] = ' ';
        }
        const char* s = argv[i];
        while (*s && pos < (int)sizeof(content) - 1)
            content[pos++] = *s++;
    }
    content[pos] = '\0';

    if (fat16_write_file(argv[1], content, (uint32_t)pos) != 0) {
        tty_puts("Write failed\n");
        return;
    }
    tty_puts("OK (");
    sh_print_uint((uint32_t)pos);
    tty_puts(" bytes)\n");
}

static void do_rm(int argc, char** argv)
{
    if (argc < 2) { tty_puts("Usage: rm <filename>\n"); return; }
    if (fat16_delete_file(argv[1]) != 0)
        tty_puts("Delete failed\n");
    else
        tty_puts("Deleted\n");
}

static void do_pwd(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts(sh_cwd);
    tty_putchar('\n');
}

/* ---- UI / LSP ---- */

static void do_ui(int argc, char** argv)
{
    (void)argc; (void)argv;
    if (!sh_fs_ready) { tty_puts("Filesystem not ready\n"); return; }

    gfx_init(800, 600);
    mouse_init();
    desktop_run(sh_fs_ready, sh_user);
    gfx_disable();
    tty_clear();
    tty_puts("LuminaOS Shell - type 'help' for commands\n\n");
}

static void do_run(int argc, char** argv)
{
    if (!sh_fs_ready) { tty_puts("Filesystem not ready\n"); return; }
    if (argc < 2) { tty_puts("Usage: run <file.lsp>\n"); return; }

    gfx_init(800, 600);
    mouse_init();
    gfx_begin();

    int r = lsp_load(argv[1]);

    gfx_disable();
    tty_clear();
    tty_puts("LuminaOS Shell - type 'help' for commands\n\n");
    if (r != 0)
        tty_puts("Failed to load program\n");
}

/* ============================================================
 *  Command table
 * ============================================================ */

typedef void (*sh_handler_t)(int argc, char** argv);

typedef struct {
    const char*  name;
    const char*  usage;
    const char*  desc;
    int          need_fs;
    int          need_login;
    sh_handler_t handler;
} shell_cmd_t;

static const shell_cmd_t sh_commands[] = {
    /* system */
    { "help",     "help",                    "Show this help",              0, 0, do_help     },
    { "clear",    "clear",                   "Clear the screen",            0, 0, do_clear    },
    { "echo",     "echo <text>",             "Echo text",                   0, 0, do_echo     },
    { "ver",      "ver",                     "Show version info",           0, 0, do_ver      },
    { "reboot",   "reboot",                  "Reboot the system",           0, 0, do_reboot   },
    { "halt",     "halt",                    "Halt the system",             0, 0, do_halt     },
    { "mem",      "mem",                     "Show memory usage",           0, 0, do_mem      },
    { "memtest",  "memtest",                 "Self-test memory allocator",  0, 0, do_memtest  },
    { "uptime",   "uptime",                  "Show system uptime",          0, 0, do_uptime   },

    /* users */
    { "register", "register <user> <pass>",  "Register a user",             0, 0, do_register },
    { "login",    "login <user> <pass>",     "Log in",                      0, 0, do_login    },
    { "logout",   "logout",                  "Log out",                     0, 0, do_logout   },
    { "users",    "users",                   "List registered users",       0, 0, do_users    },

    /* fs */
    { "ls",       "ls",                      "List files",                  1, 1, do_ls       },
    { "cd",       "cd <dir>",                "Change directory",            1, 1, do_cd       },
    { "pwd",      "pwd",                     "Print working directory",     1, 1, do_pwd      },
    { "mkdir",    "mkdir <dir>",             "Create a directory",          1, 1, do_mkdir    },
    { "cat",      "cat <file>",              "Read a file",                 1, 1, do_cat      },
    { "write",    "write <file> <content>",  "Write a file",                1, 1, do_write    },
    { "rm",       "rm <file>",               "Delete a file",               1, 1, do_rm       },

    /* ui */
    { "ui",       "ui",                      "Start the graphical UI",      1, 0, do_ui       },
    { "run",      "run <file.lsp>",          "Run an LSP program",          1, 0, do_run      },
};

#define SH_CMD_COUNT ((int)(sizeof(sh_commands) / sizeof(sh_commands[0])))

static void do_help(int argc, char** argv)
{
    (void)argc; (void)argv;
    tty_puts("Available commands:\n");
    for (int i = 0; i < SH_CMD_COUNT; i++) {
        tty_puts("  ");
        const char* u = sh_commands[i].usage;
        int len = 0;
        while (u[len]) { tty_putchar(u[len]); len++; }
        for (int pad = len; pad < 28; pad++) tty_putchar(' ');
        tty_puts("- ");
        tty_puts(sh_commands[i].desc);
        tty_putchar('\n');
    }
}

/* ============================================================
 *  Dispatch
 * ============================================================ */

static void sh_exec(char* line)
{
    char* argv[SHELL_ARG_MAX];
    int argc = 0;

    char* p = line;
    while (*p && argc < SHELL_ARG_MAX) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    if (argc == 0) return;

    for (int i = 0; i < SH_CMD_COUNT; i++) {
        if (!sh_streq(argv[0], sh_commands[i].name)) continue;

        if (sh_commands[i].need_fs && !sh_fs_ready) {
            tty_puts("Filesystem not ready\n");
            return;
        }
        if (sh_commands[i].need_login && !sh_user[0]) {
            tty_puts("Please login or register first\n");
            return;
        }
        sh_commands[i].handler(argc, argv);
        return;
    }

    tty_puts("Unknown command: ");
    tty_puts(argv[0]);
    tty_putchar('\n');
}

/* ============================================================
 *  Entry point
 *
 *  Called from kernel_main (after hardware + FS init).
 *  Never returns.
 * ============================================================ */

void _start(void)
{
    sh_fs_ready = 1;      /* caller guarantees FS is up; set 0 if not */
    sh_user[0] = '\0';
    sh_hist_count = 0;
    sh_hist_head = 0;
    sh_cwd_reset();

    tty_puts("LuminaOS Shell - type 'help' for commands\n\n");

    char line[SHELL_LINE_MAX];

    for (;;) {
        sh_prompt();
        int len = sh_read_line(line);
        if (len > 0) {
            sh_hist_add(line);
            sh_exec(line);
        }
    }
}