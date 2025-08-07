#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WIDTH 800
#define HEIGHT 600
#define GRID 20
#define ZOOM 200

float angleX = 0, angleY = 0, angleZ = 0;
float targetAngleX = 0, targetAngleY = 0, targetAngleZ = 0;
float zoom = 1.0f;
float panX = 0, panY = 0;
int last_x = 0, last_y = 0;
int rotating = 0, panning = 0;
int inside_viewcube = 0;
int viewcube_selected_face = -1;

// Animation step for snapping
#define ANGLE_ANIM_STEP 0.05f

typedef struct { float x, y, z; } Vec3;
typedef struct { int x, y; } Vec2;

float bernstein(int i, int n, float t);

Vec3 bezier_ctrl[4][4] = {
  {{-1,-1,0},{-0.3,-1,1},{0.3,-1,-1},{1,-1,0}},
  {{-1,-0.3,1},{-0.3,-0.3,2},{0.3,-0.3,0},{1,-0.3,1}},
  {{-1,0.3,0},{-0.3,0.3,1},{0.3,0.3,-1},{1,0.3,0}},
  {{-1,1,0},{-0.3,1,1},{0.3,1,-1},{1,1,0}}
};

Vec3 rotate_point(Vec3 v, float ax, float ay, float az) {
    float cx = cos(ax), sx = sin(ax);
    float cy = cos(ay), sy = sin(ay);
    float cz = cos(az), sz = sin(az);

    float x = v.x, y = v.y, z = v.z;
    float x1 = cz * x - sz * y;
    float y1 = sz * x + cz * y;
    float z1 = z;

    float x2 = cy * x1 + sy * z1;
    float y2 = y1;
    float z2 = -sy * x1 + cy * z1;

    float x3 = x2;
    float y3 = cx * y2 - sx * z2;
    float z3 = sx * y2 + cx * z2;

    return (Vec3){ x3, y3, z3 };
}

Vec3 rotate(Vec3 v) {
    return rotate_point(v, angleX, angleY, angleZ);
}

Vec3 vec_sub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vec_cross(Vec3 a, Vec3 b) {
    return (Vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
Vec3 vec_normalize(Vec3 v) {
    float len = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len == 0) return v;
    return (Vec3){v.x/len, v.y/len, v.z/len};
}
float vec_dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 vec_add(Vec3 a, Vec3 b) { return (Vec3){a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 vec_scale(Vec3 v, float s) { return (Vec3){v.x*s, v.y*s, v.z*s}; }

Vec3 bezier(float u, float v) {
    Vec3 sum = {0};
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float bu = bernstein(i, 3, u);
        float bv = bernstein(j, 3, v);
        sum.x += bu * bv * bezier_ctrl[i][j].x;
        sum.y += bu * bv * bezier_ctrl[i][j].y;
        sum.z += bu * bv * bezier_ctrl[i][j].z;
    }
    return sum;
}

float bernstein(int i, int n, float t) {
    int C[4] = {1, 3, 3, 1};
    return C[i] * pow(t, i) * pow(1 - t, n - i);
}

void put_pixel(XImage *img, int x, int y, int r, int g, int b) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    unsigned long pixel = (r << 16) | (g << 8) | b;
    ((unsigned int*)img->data)[y * WIDTH + x] = pixel;
}

void draw_line(XImage *img, int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        put_pixel(img, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_char(XImage *img, int x, int y, char c, int r, int g, int b) {
    static const char *letters[] = {
        "  X  "
        " X X "
        "X   X"
        " X X "
        "  X  ",

        "Y   Y"
        " Y Y "
        "  Y  "
        "  Y  "
        "  Y  ",

        " ZZZ "
        "   Z "
        "  Z  "
        " Z   "
        "ZZZZZ"
    };
    int idx = (c == 'X') ? 0 : (c == 'Y') ? 1 : 2;
    const char *pat = letters[idx];
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            if (pat[j*5 + i] != ' ')
                put_pixel(img, x + i, y + j, r, g, b);
}

void draw_viewcube_labels(XImage *img, int cx, int cy, int size) {
    draw_char(img, cx + size / 2 - 2, cy - size / 2 - 10, 'X', 255, 0, 0);
    draw_char(img, cx - size / 2 - 10, cy, 'Y', 0, 255, 0);
    draw_char(img, cx, cy + size / 2 + 2, 'Z', 0, 0, 255);
}

void draw_viewcube(XImage *img) {
    int size = 50;
    int cx = WIDTH - size - 10;
    int cy = 10 + size;
    Vec3 corners[8] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}
    };
    Vec2 screen[8];
    for (int i = 0; i < 8; i++) {
        Vec3 r = rotate_point(corners[i], angleX, angleY, angleZ);
        screen[i].x = cx + (int)(r.x * size / 2);
        screen[i].y = cy - (int)(r.y * size / 2);
    }
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (int i = 0; i < 12; i++) {
        draw_line(img, screen[edges[i][0]].x, screen[edges[i][0]].y,
                       screen[edges[i][1]].x, screen[edges[i][1]].y, 200, 200, 200);
    }
    for (int i = 0; i < 8; i++) {
        put_pixel(img, screen[i].x, screen[i].y, 255, 255, 255);
    }
    draw_viewcube_labels(img, cx, cy, size);
}

void check_and_apply_viewcube_snap() {
    if (viewcube_selected_face >= 0) {
        switch (viewcube_selected_face) {
            case 0: targetAngleX = 0; targetAngleY = 0; targetAngleZ = 0; break;
            case 1: targetAngleX = -M_PI/2; targetAngleY = 0; targetAngleZ = 0; break;
            case 2: targetAngleX = 0; targetAngleY = -M_PI/2; targetAngleZ = 0; break;
            case 3: targetAngleX = 0; targetAngleY = M_PI; targetAngleZ = 0; break;
            case 4: targetAngleX = M_PI/2; targetAngleY = 0; targetAngleZ = 0; break;
            case 5: targetAngleX = 0; targetAngleY = M_PI/2; targetAngleZ = 0; break;
        }
        viewcube_selected_face = -1;
    }
}

void update_animation() {
    if (fabs(angleX - targetAngleX) > ANGLE_ANIM_STEP)
        angleX += (targetAngleX - angleX) * 0.2f;
    if (fabs(angleY - targetAngleY) > ANGLE_ANIM_STEP)
        angleY += (targetAngleY - angleY) * 0.2f;
    if (fabs(angleZ - targetAngleZ) > ANGLE_ANIM_STEP)
        angleZ += (targetAngleZ - angleZ) * 0.2f;
}


void key_handler(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type != KeyPress) return;

    char buf[32];
    KeySym keysym;
    XLookupString(&event->xkey, buf, sizeof(buf), &keysym, NULL);

    switch (buf[0]) {
        case 'h':
        case 'H':
            angleX = -M_PI / 6;   // Look from top-front-right corner
            angleY = M_PI / 4;
            angleZ = 0;
            zoom = 1.0f;
            panX = panY = 0;
            XtVaSetValues(w, XmNwidth, WIDTH, XmNheight, HEIGHT, NULL);
            XtVaSetValues(w, XmNwidth, WIDTH, XmNheight, HEIGHT, NULL);
            break;
        default:
            break;
    }

    XtVaSetValues(w, XmNwidth, WIDTH, XmNheight, HEIGHT, NULL); // Optional force redraw
}


// (rest of code remains unchanged)
/*void draw_line(XImage *img, int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        put_pixel(img, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}*/

void draw_uv_grid(XImage *img) {
    for (int i = 0; i <= GRID; i++) {
        float u = (float)i / GRID;
        Vec3 prev = bezier(u, 0);
        for (int j = 1; j <= GRID; j++) {
            float v = (float)j / GRID;
            Vec3 curr = bezier(u, v);
            Vec3 p1 = rotate(prev);
            Vec3 p2 = rotate(curr);
            int x1 = WIDTH / 2 + (int)((p1.x + panX) * zoom * ZOOM);
            int y1 = HEIGHT / 2 - (int)((p1.y + panY) * zoom * ZOOM);
            int x2 = WIDTH / 2 + (int)((p2.x + panX) * zoom * ZOOM);
            int y2 = HEIGHT / 2 - (int)((p2.y + panY) * zoom * ZOOM);
            draw_line(img, x1, y1, x2, y2, 180, 180, 200);
            prev = curr;
        }
    }
    for (int j = 0; j <= GRID; j++) {
        float v = (float)j / GRID;
        Vec3 prev = bezier(0, v);
        for (int i = 1; i <= GRID; i++) {
            float u = (float)i / GRID;
            Vec3 curr = bezier(u, v);
            Vec3 p1 = rotate(prev);
            Vec3 p2 = rotate(curr);
            int x1 = WIDTH / 2 + (int)((p1.x + panX) * zoom * ZOOM);
            int y1 = HEIGHT / 2 - (int)((p1.y + panY) * zoom * ZOOM);
            int x2 = WIDTH / 2 + (int)((p2.x + panX) * zoom * ZOOM);
            int y2 = HEIGHT / 2 - (int)((p2.y + panY) * zoom * ZOOM);
            draw_line(img, x1, y1, x2, y2, 180, 180, 200);
            prev = curr;
        }
    }
}

void draw_triangle(XImage *img, Vec3 a, Vec3 b, Vec3 c, float ia, float ib, float ic) {
    int x0 = WIDTH/2 + (int)(a.x * ZOOM * zoom + panX);
    int y0 = HEIGHT/2 - (int)(a.y * ZOOM * zoom + panY);
    int x1 = WIDTH/2 + (int)(b.x * ZOOM * zoom + panX);
    int y1 = HEIGHT/2 - (int)(b.y * ZOOM * zoom + panY);
    int x2 = WIDTH/2 + (int)(c.x * ZOOM * zoom + panX);
    int y2 = HEIGHT/2 - (int)(c.y * ZOOM * zoom + panY);

    int cx = (x0 + x1 + x2) / 3;
    int cy = (y0 + y1 + y2) / 3;
    float brightness = (ia + ib + ic) / 3.0f;
    brightness = 0.2f + brightness * 0.8f;
    int shade = (int)(brightness * 255);
    put_pixel(img, cx, cy, shade, shade, shade);
}

void draw_scene(Display *dpy, Window win, GC gc, Visual *visual, int depth) {
    XImage *img = XCreateImage(dpy, visual, depth, ZPixmap, 0,
        malloc(WIDTH * HEIGHT * 4), WIDTH, HEIGHT, 32, 0);

    memset(img->data, 0, WIDTH * HEIGHT * 4);
    Vec3 light = vec_normalize((Vec3){1, 1, -1});

    for (int i = 0; i < GRID; i++) for (int j = 0; j < GRID; j++) {
        float u = (float)i / GRID, v = (float)j / GRID;
        float u1 = (float)(i+1) / GRID, v1 = (float)(j+1) / GRID;

        Vec3 p00 = rotate(bezier(u, v));
        Vec3 p10 = rotate(bezier(u1, v));
        Vec3 p01 = rotate(bezier(u, v1));
        Vec3 p11 = rotate(bezier(u1, v1));

        Vec3 n00 = vec_normalize(vec_cross(vec_sub(p10, p00), vec_sub(p01, p00)));
        Vec3 n10 = vec_normalize(vec_cross(vec_sub(p11, p10), vec_sub(p00, p10)));
        Vec3 n01 = vec_normalize(vec_cross(vec_sub(p00, p01), vec_sub(p11, p01)));
        Vec3 n11 = vec_normalize(vec_cross(vec_sub(p01, p11), vec_sub(p10, p11)));

        float i00 = vec_dot(n00, light);
        float i10 = vec_dot(n10, light);
        float i01 = vec_dot(n01, light);
        float i11 = vec_dot(n11, light);

        draw_triangle(img, p00, p10, p11, i00, i10, i11);
        draw_triangle(img, p00, p11, p01, i00, i11, i01);
    }

    draw_viewcube(img);
    draw_uv_grid(img);
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, WIDTH, HEIGHT);
    XDestroyImage(img);
}

void redisplay(Widget w) {
    Display *dpy = XtDisplay(w);
    Window win = XtWindow(w);
    GC gc = XCreateGC(dpy, win, 0, NULL);
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, win, &attr);
    draw_scene(dpy, win, gc, attr.visual, attr.depth);
    XFreeGC(dpy, gc);
}

void motion_cb(Widget w, XtPointer d, XEvent *e, Boolean *c) {
    XMotionEvent *ev = (XMotionEvent*)e;
    int dx = ev->x - last_x, dy = ev->y - last_y;
    last_x = ev->x; last_y = ev->y;

    if (rotating) {
        if (inside_viewcube) {
            angleX = dy * 0.01;
            angleY = dx * 0.01;
            angleZ = 0;
        } else if (ev->state & ShiftMask) angleZ += dx * 0.01f;
        else {
            angleY += dx * 0.01f;
            angleX += dy * 0.01f;
        }
    } else if (panning) {
        panX += dx;
        panY += dy;
    }
    redisplay(w);
}

void button_cb(Widget w, XtPointer d, XEvent *e, Boolean *c) {
    XButtonEvent *ev = (XButtonEvent*)e;
    if (ev->type == ButtonPress) {
        last_x = ev->x;
        last_y = ev->y;
        inside_viewcube = (ev->x > WIDTH - 70 && ev->y < 70);

        if (inside_viewcube) {
            int rel_x = ev->x - (WIDTH - 40);
            int rel_y = ev->y - 10;
            if (rel_x < 20 && rel_y < 20) {
                // top
                angleX = -1.57f; angleY = 0; angleZ = 0;
            } else if (rel_x > 20 && rel_y > 20) {
                // bottom right
                angleX = 1.57f; angleY = 0; angleZ = 0;
            } else {
                // front
                angleX = 0; angleY = 0; angleZ = 0;
            }
            redisplay(w);
            return;
        }

        if (ev->button == Button1) rotating = 1;
        else if (ev->button == Button2) panning = 1;
        else if (ev->button == Button4 || ev->button == Button5) {
            float mx = ev->x - WIDTH/2 - panX;
            float my = -(ev->y - HEIGHT/2) - panY;
            float factor = (ev->button == Button4) ? 1.1f : 0.9f;
            zoom *= factor;
            panX -= mx * (factor - 1);
            panY -= my * (factor - 1);
            redisplay(w);
        }
    } else if (ev->type == ButtonRelease) {
        rotating = 0; panning = 0;
        inside_viewcube = 0;
    }
}

void expose_cb(Widget w, XtPointer d, XEvent *e, Boolean *c) {
    redisplay(w);
}

int main(int argc, char **argv) {
    XtAppContext app;
    Widget top = XtVaAppInitialize(&app, "Bezier3D", NULL, 0, &argc, argv, NULL, NULL);
    Widget draw = XtVaCreateManagedWidget("draw", xmDrawingAreaWidgetClass, top,
        XmNwidth, WIDTH, XmNheight, HEIGHT, NULL);
    XtAddEventHandler(draw, ExposureMask, False, expose_cb, NULL);
    XtAddEventHandler(draw, ButtonPressMask | ButtonReleaseMask, False, button_cb, NULL);
    XtAddEventHandler(draw, PointerMotionMask, False, motion_cb, NULL);
    XtAddEventHandler(draw, KeyPressMask, False, key_handler, NULL);
    XtSetKeyboardFocus(top, draw);    
    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}