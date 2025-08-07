/* viewcube.c - Interactive 3D ViewCube with isometric projection (X11 + Motif, no GL)
 *
 * Features:
 * - Isometric 3D projection
 * - 6 face labels rendered via Xft (UTF-8)
 * - Highlight face/edge/corner on mouse hover
 * - Click to rotate
 * - Smooth animation (disabled)
 * - Face/edge/corner click detection
 */

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/DrawingA.h>
#include <X11/Xft/Xft.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define DEG2RAD(d) ((d) * M_PI / 180.0)
#define TIMER_INTERVAL 20 // ms

static Pixmap back_buffer = 0;
static int buffer_width = 0;
static int buffer_height = 0;

// ========================= 3D Cube Geometry =============================

typedef struct { double x, y, z; } Vec3;
typedef struct { int v[4]; char *label; Vec3 normal; } Face;

Vec3 cube_vertices[8] = {
    {-1, -1, -1}, {+1, -1, -1}, {+1, +1, -1}, {-1, +1, -1},
    {-1, -1, +1}, {+1, -1, +1}, {+1, +1, +1}, {-1, +1, +1}
};

Face cube_faces[6] = {
    {{0,1,2,3}, "下", { 0,  0, -1}},
    {{4,5,6,7}, "上", { 0,  0, +1}},
    {{0,4,7,3}, "左", {-1,  0,  0}},
    {{1,5,6,2}, "右", {+1,  0,  0}},
    {{0,1,5,4}, "前", { 0, -1,  0}},
    {{3,2,6,7}, "後", { 0, +1,  0}}
};

int cube_edges[12][2] = {
    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
};

int cube_corners[8] = {0,1,2,3,4,5,6,7};

// ======================= Vector Math ====================================

Vec3 rotate(Vec3 v, double ax, double ay, double az) {
    double cx = cos(ax), sx = sin(ax);
    double cy = cos(ay), sy = sin(ay);
    double cz = cos(az), sz = sin(az);
    // Rotate X
    double y = v.y * cx - v.z * sx;
    double z = v.y * sx + v.z * cx;
    v.y = y; v.z = z;
    // Rotate Y
    double x = v.x * cy + v.z * sy;
    z = -v.x * sy + v.z * cy;
    v.x = x; v.z = z;
    // Rotate Z
    x = v.x * cz - v.y * sz;
    y = v.x * sz + v.y * cz;
    v.x = x; v.y = y;
    return v;
}

// ======================= Projection =====================================

typedef struct { int x, y; } Vec2;
Vec2 project(Vec3 v, int width, int height, double scale) {
    return (Vec2){
        width/2 + (int)(v.x * scale),
        height/2 - (int)(v.y * scale)
    };
}

// ======================= Xft Text Rendering ==============================

XImage* render_label(Display *dpy, Visual *visual, int screen, const char *utf8, int *w_out, int *h_out) {
    int w = 32, h = 32;
    Pixmap pixmap = XCreatePixmap(dpy, RootWindow(dpy, screen), w, h, DefaultDepth(dpy, screen));
    XftDraw *draw = XftDrawCreate(dpy, pixmap, visual, DefaultColormap(dpy, screen));

    XftColor color;
    XRenderColor rc = {0, 0, 0, 65535};
    XftColorAllocValue(dpy, visual, DefaultColormap(dpy, screen), &rc, &color);

    XftFont *font = XftFontOpenName(dpy, screen, "Noto Sans CJK JP-16");
    if (!font) font = XftFontOpenName(dpy, screen, "Sans-16");

    XftDrawRect(draw, &color, 0, 0, w, h);
    XftDrawStringUtf8(draw, &color, font, 8, 24, (const FcChar8 *)utf8, strlen(utf8));

    XImage *img = XGetImage(dpy, pixmap, 0, 0, w, h, AllPlanes, ZPixmap);

    XftFontClose(dpy, font);
    XftColorFree(dpy, visual, DefaultColormap(dpy, screen), &color);
    XftDrawDestroy(draw);
    XFreePixmap(dpy, pixmap);

    if (w_out) *w_out = w;
    if (h_out) *h_out = h;
    return img;
}

// ======================= Drawing & Callbacks ============================

static double angle_x = DEG2RAD(35.264);  // isometric
static double angle_y = DEG2RAD(45);
static double target_ax = DEG2RAD(35.264);
static double target_ay = DEG2RAD(45);
static int hover_face = -1, hover_edge = -1, hover_corner = -1;
static Widget global_widget;

void draw_cube(Display *dpy, Drawable drawable, GC gc, Visual *visual, int screen,
               int width, int height, int mouse_x, int mouse_y){
    Vec3 rotated[8];
    for (int i = 0; i < 8; i++)
        rotated[i] = rotate(cube_vertices[i], angle_x, angle_y, 0);

    Vec2 projected[8];
    for (int i = 0; i < 8; i++)
        projected[i] = project(rotated[i], width, height, 60);

    hover_face = hover_edge = hover_corner = -1;

    for (int i = 0; i < 8; i++) {
        int dx = projected[i].x - mouse_x, dy = projected[i].y - mouse_y;
        if (dx*dx + dy*dy < 10*10) hover_corner = i;
    }

    for (int i = 0; i < 12; i++) {
        int x1 = projected[cube_edges[i][0]].x, y1 = projected[cube_edges[i][0]].y;
        int x2 = projected[cube_edges[i][1]].x, y2 = projected[cube_edges[i][1]].y;
        int mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
        int dx = mx - mouse_x, dy = my - mouse_y;
        if (dx*dx + dy*dy < 10*10) hover_edge = i;
    }

    for (int f = 0; f < 6; f++) {
        Face face = cube_faces[f];
        Vec3 center = {0,0,0};
        for (int i = 0; i < 4; i++) {
            center.x += rotated[face.v[i]].x;
            center.y += rotated[face.v[i]].y;
            center.z += rotated[face.v[i]].z;
        }
        center.x /= 4; center.y /= 4; center.z /= 4;

        Vec2 c = project(center, width, height, 60);
        int dx = c.x - mouse_x, dy = c.y - mouse_y;
        int dist2 = dx*dx + dy*dy;
        if (dist2 < 20*20) hover_face = f;

        XSetForeground(dpy, gc, (f == hover_face) ? 0xff0000 : 0xcccccc);
        XPoint pts[5];
        for (int i = 0; i < 4; i++) {
            pts[i].x = projected[face.v[i]].x;
            pts[i].y = projected[face.v[i]].y;
        }
        pts[4] = pts[0];
        XFillPolygon(dpy, drawable, gc, pts, 4, Convex, CoordModeOrigin);

        XImage *img = render_label(dpy, visual, screen, face.label, NULL, NULL);
        if (img){
            int label_x = c.x - img->width / 2;
            int label_y = c.y - img->height / 2;
            XPutImage(dpy, drawable, gc, img, 0, 0, label_x, label_y, img->width, img->height);
            XDestroyImage(img);
        }
    }

    for (int i = 0; i < 12; i++) {
        int color = (i == hover_edge) ? 0x00aa00 : 0x000000;
        XSetForeground(dpy, gc, color);
        XDrawLine(dpy, drawable, gc,
            projected[cube_edges[i][0]].x, projected[cube_edges[i][0]].y,
            projected[cube_edges[i][1]].x, projected[cube_edges[i][1]].y);
    }

    if (hover_corner >= 0) {
        XSetForeground(dpy, gc, 0x0000ff);
        XFillArc(dpy, drawable, gc, projected[hover_corner].x - 4, projected[hover_corner].y - 4, 8, 8, 0, 360*64);
    }
}

void expose_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    Display *dpy = XtDisplay(w);
    Window win = XtWindow(w);
    GC gc = DefaultGC(dpy, DefaultScreen(dpy));

    Dimension width, height;
    XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);

    if (back_buffer)
        XCopyArea(dpy, back_buffer, win, gc, 0, 0, width, height, 0, 0);
}

void motion_cb(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type != MotionNotify) return;
    XMotionEvent *e = (XMotionEvent *)event;
    int *mouse_xy = (int *)client_data;
    mouse_xy[0] = e->x;
    mouse_xy[1] = e->y;

    Display *dpy = XtDisplay(w);
    Window win = XtWindow(w);
    GC gc = DefaultGC(dpy, DefaultScreen(dpy));
    Visual *visual = DefaultVisual(dpy, DefaultScreen(dpy));
    int screen = DefaultScreen(dpy);

    Dimension width, height;
    XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);

    // Create or resize back buffer
    if (!back_buffer || width != buffer_width || height != buffer_height) {
        if (back_buffer)
            XFreePixmap(dpy, back_buffer);
        back_buffer = XCreatePixmap(dpy, win, width, height, DefaultDepth(dpy, screen));
        buffer_width = width;
        buffer_height = height;
    }

    draw_cube(dpy, back_buffer, gc, visual, screen, width, height, mouse_xy[0], mouse_xy[1]);
    XCopyArea(dpy, back_buffer, win, gc, 0, 0, width, height, 0, 0);
}

void click_cb(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    if (event->type != ButtonPress) return;
    if (hover_face >= 0) {
        switch (hover_face) {
            case 0: angle_x = DEG2RAD(-90); break;
            case 1: angle_x = DEG2RAD(90); break;
            case 2: angle_y -= DEG2RAD(90); break;
            case 3: angle_y += DEG2RAD(90); break;
            case 4: angle_y = DEG2RAD(0); break;
            case 5: angle_y = DEG2RAD(180); break;
        }
    } else if (hover_edge >= 0) {
        angle_x += DEG2RAD(15);
        angle_y += DEG2RAD(15);
    } else if (hover_corner >= 0) {
        angle_x += DEG2RAD(30);
        angle_y -= DEG2RAD(30);
    }
    XClearArea(XtDisplay(w), XtWindow(w), 0, 0, 0, 0, True);
}

int main(int argc, char *argv[]) {
    XtAppContext app;
    Widget toplevel = XtVaAppInitialize(&app, "ViewCube", NULL, 0, &argc, argv, NULL, NULL);
    Widget drawing = XtVaCreateManagedWidget("drawing", xmDrawingAreaWidgetClass, toplevel,
        XmNwidth, 300, XmNheight, 300, NULL);

    global_widget = drawing;
    static int mouse_xy[2] = {150, 150};
    XtAddCallback(drawing, XmNexposeCallback, expose_cb, mouse_xy);
    XtAddEventHandler(drawing, PointerMotionMask, False, motion_cb, mouse_xy);
    XtAddEventHandler(drawing, ButtonPressMask, False, click_cb, mouse_xy);

    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);
}
