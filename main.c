#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_ITEMS 64
#define ITEM_HEIGHT 30
#define WINDOW_WIDTH 250

typedef struct {
    char name[64];
    char command[256];
} MenuItem;

MenuItem menu_items[MAX_ITEMS];
int item_count = 0;

// Удаление пробелов и переносов строк в начале и конце
void trim(char *s) {
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) *--p = '\0';
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    memmove(s, start, strlen(start) + 1);
}

void load_config() {
    // Пытаемся открыть menu.conf в текущей директории
    FILE *f = fopen("menu.conf", "r");
    if (!f) {
        printf("Создаю дефолтный menu.conf...\n");
        f = fopen("menu.conf", "w");
        fprintf(f, "LibreWolf;librewolf\nTerm;alacritty\nEditor;alacritty -e nvim\nUpdate;xterm -e \"sudo pacman -Syu\"\n");
        fclose(f);
        f = fopen("menu.conf", "r");
    }

    char line[512];
    while (fgets(line, sizeof(line), f) && item_count < MAX_ITEMS) {
        trim(line);
        if (strlen(line) < 3 || line[0] == '#') continue;

        char *sep = strchr(line, ';');
        if (sep) {
            *sep = '\0';
            char *name = line;
            char *cmd = sep + 1;
            trim(name);
            trim(cmd);

            strncpy(menu_items[item_count].name, name, 63);
            strncpy(menu_items[item_count].command, cmd, 255);
            item_count++;
        }
    }
    fclose(f);
}

void execute_cmd(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(0);
    }
}

void draw_menu(Display *dpy, Window win, GC gc, int selected, int screen) {
    XClearWindow(dpy, win);
    for (int i = 0; i < item_count; i++) {
        if (i == selected) {
            XSetForeground(dpy, gc, 0x333333); // Цвет выделения (темно-серый)
            XFillRectangle(dpy, win, gc, 0, i * ITEM_HEIGHT, WINDOW_WIDTH, ITEM_HEIGHT);
            XSetForeground(dpy, gc, WhitePixel(dpy, screen));
        } else {
            XSetForeground(dpy, gc, BlackPixel(dpy, screen));
        }
        // Рисуем текст (смещение 10px слева, 20px сверху внутри пункта)
        XDrawString(dpy, win, gc, 15, i * ITEM_HEIGHT + 20, menu_items[i].name, strlen(menu_items[i].name));
        
        XSetForeground(dpy, gc, 0xEEEEEE); // Тонкая линия разделителя
        XDrawLine(dpy, win, gc, 0, (i + 1) * ITEM_HEIGHT, WINDOW_WIDTH, (i + 1) * ITEM_HEIGHT);
    }
}

int main() {
    load_config();
    if (item_count == 0) {
        fprintf(stderr, "Ошибка: Конфиг пуст или не найден\n");
        return 1;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    int screen = DefaultScreen(dpy);
    int win_height = item_count * ITEM_HEIGHT;

    // Получаем позицию мыши
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(dpy, RootWindow(dpy, screen), &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True; // Важно для Tiling WM!
    attrs.background_pixel = WhitePixel(dpy, screen);
    attrs.event_mask = ExposureMask | ButtonPressMask | PointerMotionMask;

    Window win = XCreateWindow(dpy, RootWindow(dpy, screen), 
                              root_x, root_y, WINDOW_WIDTH, win_height, 
                              1, CopyFromParent, InputOutput, CopyFromParent,
                              CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);

    XMapWindow(dpy, win);

    // Захватываем мышь, чтобы закрыть меню при клике в любом месте экрана
    XGrabPointer(dpy, RootWindow(dpy, screen), True, ButtonPressMask, 
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    // Пытаемся загрузить шрифт
    XFontStruct *font = XLoadQueryFont(dpy, "9x15");
    if (!font) font = XLoadQueryFont(dpy, "fixed");
    if (font) XSetFont(dpy, gc, font->fid);

    int selected_item = -1;
    XEvent ev;
    int running = 1;

    while (running) {
        XNextEvent(dpy, &ev);

        if (ev.type == Expose) {
            draw_menu(dpy, win, gc, selected_item, screen);
        }

        if (ev.type == MotionNotify) {
            // Вычисляем положение мыши относительно окна меню
            int mx = ev.xmotion.x_root - root_x;
            int my = ev.xmotion.y_root - root_y;
            
            int old_selected = selected_item;
            if (mx >= 0 && mx <= WINDOW_WIDTH && my >= 0 && my <= win_height) {
                selected_item = my / ITEM_HEIGHT;
            } else {
                selected_item = -1;
            }

            if (old_selected != selected_item) {
                draw_menu(dpy, win, gc, selected_item, screen);
            }
        }

        if (ev.type == ButtonPress) {
            int mx = ev.xbutton.x_root - root_x;
            int my = ev.xbutton.y_root - root_y;
            
            if (mx >= 0 && mx <= WINDOW_WIDTH && my >= 0 && my <= win_height) {
                int i = my / ITEM_HEIGHT;
                if (i >= 0 && i < item_count) {
                    execute_cmd(menu_items[i].command);
                }
            }
            running = 0; // Закрыть при любом клике
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
