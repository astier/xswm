#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>

// EWMH-Atoms
enum {
    ActiveWindow,
    ClientList,
    ClientListStacking,
    CloseWindow,
    CurrentDesktop,
    DesktopGeometry,
    DesktopNames,
    DesktopViewport,
    FrameExtents,
    NumberOfDesktops,
    RequestFrameExtents,
    Supported,
    SupportingWMCheck,
    WMDesktop,
    WMFullPlacement,
    WMName,
    WMWindowType,
    WMWindowTypeDialog,
    WMWindowTypeNormal,
    WMWindowTypeSplash,
    WMWindowTypeUtility,
    Workarea,
    Net_N
};

// ICCCM-Atoms
enum {
    DeleteWindow,
    Protocols,
    State,
    WM_N
};

typedef struct Client {
    const Window w;
    Bool fixed, normal;
    int width_request, height_request;
    int x, y, width, height;
    struct Client *next;
} Client;

// Event-Handlers
static void button_press(const XButtonPressedEvent *);
static void client_message(const XClientMessageEvent *);
static void configure_notify(const XConfigureEvent *);
static void configure_request(const XConfigureRequestEvent *);
static void focus_in(const XFocusInEvent *);
static void map_request(Window);
static void property_notify(const XPropertyEvent *);
static void unmap_notify(const XUnmapEvent *);

// List-Functions
static Client * get_client(Window);
static Client * get_parent(const Client *);

// Remote-Commands
static void close(void);
static void last(void);
static void quit(void);

// Window-Management
static void delete(Window);
static void focus(Window);
static void pop(Window);
static void resize(Client *);
static void update_client_list(Window);
static void update_client_list_stacking(void);
static void update_geometry(Client *);

// Window-State
static Bool is_fixed(Window);
static Bool is_normal(Window);
static Bool is_floating(const Client *);
static int  get_state(Window);

// X-Error-Handler
static int xerror(Display *, XErrorEvent *);
static int xerror_start(Display *, XErrorEvent *);

// Atoms
static Atom net_atoms[Net_N];
static Atom wm_atoms[WM_N];
static Atom XA_WM_CMD;

// Geometry
static const int BORDER_WIDTH = 1;
static int sw, sh; // screen-width and -height

// Linked-List
static Client *head; // Top-window and start of a linked-list
static int clients_n = 0; // Number of clients in list

// Misc
static Bool running = True;
static Display *d;
static Window r; // root-window

void button_press(const XButtonPressedEvent *e) {
    pop(e->window);
    XAllowEvents(d, ReplayPointer, CurrentTime);
}

void client_message(const XClientMessageEvent *e) {
    const Window w = e->window;
    if (!get_client(w))
        return;
    const Atom msg = e->message_type;
    if (msg == net_atoms[ActiveWindow])
        pop(w);
    else if (msg == net_atoms[CloseWindow])
        delete(w);
    else if (msg == net_atoms[RequestFrameExtents])
        XChangeProperty(d, w, net_atoms[FrameExtents], XA_CARDINAL, 32,
            PropModeReplace, (unsigned char *) (long []) {BORDER_WIDTH,
            BORDER_WIDTH, BORDER_WIDTH, BORDER_WIDTH}, 4);
}

void configure_notify(const XConfigureEvent *e) {
    const int width  = e->width;
    const int height = e->height;
    if (e->window != r || (sw == width && sh == height))
        return;
    sw = width, sh = height;
    XChangeProperty(d, r, net_atoms[DesktopGeometry], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {sw, sh}, 2);
    XChangeProperty(d, r, net_atoms[Workarea], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0, sw, sh}, 4);
    for (Client *c = head; c; c = c->next)
        resize(c);
}

void configure_request(const XConfigureRequestEvent *e) {
    Client *c;
    const Window w = e->window;
    const unsigned long value_mask = e->value_mask;
    if ((c = get_client(w))) {
        if (value_mask & CWWidth) c->width_request = e->width;
        if (value_mask & CWHeight) c->height_request = e->height;
        if (value_mask & (CWWidth | CWHeight) && is_floating(c))
            resize(c);
        else XSendEvent(d, w, False, StructureNotifyMask, (XEvent *) &(XConfigureEvent) {
                .type = ConfigureNotify,
                .send_event = True,
                .display = d,
                .event = w,
                .window = w,
                .x = c->x,
                .y = c->y,
                .width = c->width,
                .height = c->height,
                .border_width = BORDER_WIDTH,
                .above = None,
                .override_redirect = False,
            });
    } else XConfigureWindow(d, w, (unsigned int) value_mask, &(XWindowChanges) {
        .x = e->x,
        .y = e->y,
        .width = e->width,
        .height = e->height,
        .border_width = e->border_width,
        .sibling = e->above,
        .stack_mode = e->detail,
    });
}

// Prevent bad clients from stealing focus
void focus_in(const XFocusInEvent *e) {
    if (head && head->w != e->window)
        focus(head->w);
}

void map_request(const Window w) {
    if (get_client(w))
        return;
    // Get geometry
    unsigned int width  = (unsigned int) sw;
    unsigned int height = (unsigned int) sh;
    XGetGeometry(d, w, &(Window) {None}, &(int) {None}, &(int) {None},
        &width, &height, &(unsigned int) {None}, &(unsigned int) {None});
    // Initialize client and add to linked-list
    memcpy(head = malloc(sizeof(Client)), &(Client) {
        w, is_fixed(w), is_normal(w), (int) width, (int) height,
        None, None, None, None, head}, sizeof(Client));
    update_geometry(head);
    clients_n++;
    // Update ewmh-client-lists
    XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
        PropModeAppend, (unsigned char *) &w, 1);
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeAppend, (unsigned char *) &w, 1);
    // Configure
    XChangeProperty(d, w, net_atoms[FrameExtents], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {BORDER_WIDTH,
        BORDER_WIDTH, BORDER_WIDTH, BORDER_WIDTH}, 4);
    XChangeProperty(d, w, net_atoms[WMDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (int []) {0}, 1);
    XGrabButton(d, AnyButton, AnyModifier, w, True, ButtonPressMask,
        GrabModeSync, GrabModeSync, None, None);
    XSelectInput(d, w, FocusChangeMask | PropertyChangeMask);
    XConfigureWindow(d, w, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &(XWindowChanges) {
        .x = head->x,
        .y = head->y,
        .width = head->width,
        .height = head->height,
        .border_width = BORDER_WIDTH,
    });
    // Map
    XChangeProperty(d, w, wm_atoms[State], wm_atoms[State], 32,
        PropModeReplace, (unsigned char *) (long []) {NormalState, None}, 2);
    XMapWindow(d, w);
    focus(w);
}

void property_notify(const XPropertyEvent *e) {
    Client *c;
    const Window w = e->window;
    const Atom property = e->atom;
    if (w == r) {
        // Remote-Control
        if (property != XA_WM_CMD)
            return;
        XTextProperty p;
        if (!XGetTextProperty(d, r, &p, XA_WM_CMD) || !p.value)
            return;
        const int cmd_size = 8;
        char cmd[cmd_size];
        strncpy(cmd, (char *) p.value, cmd_size - 1);
        cmd[cmd_size - 1] = '\0';
        if      (!strcmp(cmd, "last"))  last();
        else if (!strcmp(cmd, "close")) close();
        else if (!strcmp(cmd, "quit"))  quit();
        XFree(p.value);
    } else if ((c = get_client(w)) && (property == XA_WM_NORMAL_HINTS
            || property == net_atoms[WMWindowType])) {
        Bool floating_old = is_floating(c);
        if (property == XA_WM_NORMAL_HINTS)
            c->fixed = is_fixed(w);
        else if (property == net_atoms[WMWindowType])
            c->normal = is_normal(w);
        if (floating_old != is_floating(c))
            resize(c);
    }
}

void unmap_notify(const XUnmapEvent *e) {
    const Window w = e->window;
    Client *c = get_client(w);
    if (!c)
        return;
    // Handle window
    XGrabServer(d); // Avoid race-conditions
    XSelectInput(d, w, NoEventMask);
    XUngrabButton(d, AnyButton, AnyModifier, w);
    XDeleteProperty(d, w, net_atoms[WMDesktop]);
    XChangeProperty(d, w, wm_atoms[State], wm_atoms[State], 32,
        PropModeReplace, (unsigned char *) (long []) {WithdrawnState, None}, 2);
    XSync(d, False);
    XUngrabServer(d);
    // Update list
    if (c != head)
        get_parent(c)->next = c->next;
    else if (clients_n == 1) {
        XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW, 32,
            PropModeReplace, None, 0);
        head = NULL;
    } else {
        head = head->next;
        focus(head->w);
    }
    free(c);
    clients_n--;
    update_client_list(w);
    update_client_list_stacking();
}

Client * get_client(const Window w) {
    Client *c;
    for (c = head; c && c->w != w; c = c->next);
    return c;
}

Client * get_parent(const Client *c) {
    if (!c || c == head)
        return NULL;
    Client *p;
    for (p = head; p && p->next != c; p = p->next);
    return p;
}

void close(void) { if (clients_n > 0) delete(head->w); }

void last(void) { if (clients_n > 1) pop(head->next->w); }

void quit(void) { running = False; }

void delete(const Window w) {
    Atom protocol = wm_atoms[DeleteWindow];
    Atom *protocols;
    int count;
    if (!XGetWMProtocols(d, w, &protocols, &count))
        return;
    Bool protocol_exists = False;
    while (!protocol_exists && count--)
        protocol_exists = protocols[count] == protocol;
    XFree(protocols);
    if (!protocol_exists)
        return;
    XSendEvent(d, w, False, NoEventMask, (XEvent *) &(XClientMessageEvent) {
        .type = ClientMessage,
        .window = w,
        .message_type = wm_atoms[Protocols],
        .format = 32,
        .data.l[0] = (long) protocol,
        .data.l[1] = CurrentTime,
    });
}

void focus(const Window w) {
    XSetInputFocus(d, w, RevertToPointerRoot, CurrentTime);
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW,
        32, PropModeReplace, (unsigned char *) &w, 1);
}

void pop(const Window w) {
    Client *c = get_client(w);
    if (!c || head == c)
        return;
    get_parent(c)->next = c->next;
    c->next = head;
    head = c;
    focus(w);
    XRaiseWindow(d, w);
    update_client_list_stacking();
}

void resize(Client *c) {
    update_geometry(c);
    XMoveResizeWindow(d, c->w, c->x, c->y, (unsigned int) c->width,
        (unsigned int) c->height);
}

void update_client_list(const Window w) {
    // Get _NET_CLIENT_LIST
    unsigned long nitems;
    unsigned char *prop = NULL;
    if (XGetWindowProperty(d, r, net_atoms[ClientList], 0, ~0, False, XA_WINDOW,
    &(Atom) {None}, &(int) {None}, &nitems, &(unsigned long) {None}, &prop)
    != Success || !prop || !nitems)
        return;
    Window *client_list = (Window *) prop;
    // Get window-index
    unsigned long i;
    for (i = 0; i < nitems && client_list[i] != w; i++);
    // Remove w from _NET_CLIENT_LIST
    if (i < nitems) {
        memmove(&client_list[i], &client_list[i + 1], (--nitems - i) * sizeof(Window));
        XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
            PropModeReplace, (unsigned char *) client_list, (int) nitems);
    }
    XFree(prop);
}

void update_client_list_stacking(void) {
    Window clients[clients_n];
    int i = clients_n - 1;
    for (Client *c = head; c; c = c->next)
        clients[i--] = c->w;
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) clients, clients_n);
}

void update_geometry(Client *c) {
    c->x = c->y = -BORDER_WIDTH, c->width = sw, c->height = sh;
    if (is_floating(c)) {
        // Center if smaller than screen
        const int true_width = c->width_request + BORDER_WIDTH * 2;
        if (true_width < sw) {
            c->x = (sw - true_width) / 2;
            c->width = c->width_request;
        }
        const int true_height = c->height_request + BORDER_WIDTH * 2;
        if (true_height < sh) {
            c->y = (sh - true_height) / 2;
            c->height = c->height_request;
        }
    }
}

Bool is_fixed(const Window w) {
    XSizeHints hints;
    if (!XGetWMNormalHints(d, w, &hints, &(long) {None}))
        return False;
    int min_width, min_height;
    if (hints.flags & PMinSize) {
        min_width = hints.min_width;
        min_height = hints.min_height;
    } else if (hints.flags & PBaseSize) {
        min_width = hints.base_width;
        min_height = hints.base_height;
    } else
        return False;
    if ((hints.flags & PMaxSize)
            && min_width  == hints.max_width
            && min_height == hints.max_height)
        return True;
    return False;
}

Bool is_normal(const Window w) {
    unsigned char *prop = NULL;
    if (XGetWindowProperty(d, w, net_atoms[WMWindowType], 0, 1, False,
    XA_ATOM, &(Atom) {None}, &(int) {None}, &(unsigned long) {None},
    &(unsigned long) {None}, &prop) != Success)
        return True;
    Bool normal = True;
    if (prop) {
        normal = *(Atom *) prop == net_atoms[WMWindowTypeNormal] ? True : False;
        XFree(prop);
    } else if (XGetTransientForHint(d, w, &(Window) {None}))
        normal = False;
    return normal;
}

Bool is_floating(const Client *c) { return c->fixed || !c->normal; }

int get_state(const Window w) {
    int state = -1;
    unsigned char *prop = NULL;
    unsigned long nitems;
    if (XGetWindowProperty(d, w, wm_atoms[State], 0, 2, False, wm_atoms[State],
    &(Atom) {None}, &(int) {None}, &nitems, &(unsigned long) {None},
    (unsigned char **) &prop) != Success || !prop || !nitems)
        return state;
    state = *(int *) prop;
    XFree(prop);
    return state;
}

int xerror(Display *dpy, XErrorEvent *e) { (void) dpy; (void) e; return 0; }

int xerror_start(Display *dpy, XErrorEvent *e) {
    (void) dpy;
    if (e->error_code == BadAccess) {
        fprintf(stderr, "Error: Another window manager is already running.\n");
        exit(EXIT_FAILURE);
    }
    return -1;
}

int main(const int argc, const char *argv[]) {
    if (!(d = XOpenDisplay(NULL))) {
        fprintf(stderr, "Error: Unable to open display.\n");
        return EXIT_FAILURE;
    }
    // Remote-Control by setting the _XSWM_CMD property on the root-window
    // and catching it with property_notify()
    // Check property_notify() to see which commands are supported
    r = XDefaultRootWindow(d);
    XA_WM_CMD = XInternAtom(d, "_XSWM_CMD", False);
    if (argc > 1) {
        XChangeProperty(d, r, XA_WM_CMD, XA_STRING, 8,
            PropModeReplace, (unsigned char *) argv[1], (int) strlen(argv[1]));
        XCloseDisplay(d);
        return EXIT_SUCCESS;
    }
    // Check if another window-manager is already running
    XSetErrorHandler(xerror_start);
    XSelectInput(d, r, SubstructureRedirectMask);
    XSync(d, False);
    XSetErrorHandler(xerror);
    XSync(d, False);
    // Handle signals
    signal(SIGCHLD, SIG_IGN);
    // Variables
    const int s = XDefaultScreen(d);
    sh = XDisplayHeight(d, s);
    sw = XDisplayWidth(d, s);
    // ICCCM-Atoms
    char *wm_atom_names[WM_N];
    wm_atom_names[Protocols] = "WM_PROTOCOLS";
    wm_atom_names[DeleteWindow] = "WM_DELETE_WINDOW";
    wm_atom_names[State] = "WM_STATE";
    XInternAtoms(d, wm_atom_names, WM_N, False, wm_atoms);
    // EWMH-Atoms
    char *net_atom_names[Net_N];
    net_atom_names[ActiveWindow] = "_NET_ACTIVE_WINDOW";
    net_atom_names[ClientList] = "_NET_CLIENT_LIST";
    net_atom_names[ClientListStacking] = "_NET_CLIENT_LIST_STACKING";
    net_atom_names[CloseWindow] = "_NET_CLOSE_WINDOW";
    net_atom_names[FrameExtents] = "_NET_FRAME_EXTENTS";
    net_atom_names[RequestFrameExtents] = "_NET_REQUEST_FRAME_EXTENTS";
    net_atom_names[Supported] = "_NET_SUPPORTED";
    net_atom_names[SupportingWMCheck] = "_NET_SUPPORTING_WM_CHECK";
    net_atom_names[WMFullPlacement] = "_NET_WM_FULL_PLACEMENT";
    net_atom_names[WMName] = "_NET_WM_NAME";
    net_atom_names[Workarea] = "_NET_WORKAREA";
    // Desktops
    net_atom_names[CurrentDesktop] = "_NET_CURRENT_DESKTOP";
    net_atom_names[DesktopGeometry] = "_NET_DESKTOP_GEOMETRY";
    net_atom_names[DesktopNames] = "_NET_DESKTOP_NAMES";
    net_atom_names[DesktopViewport] = "_NET_DESKTOP_VIEWPORT";
    net_atom_names[NumberOfDesktops] = "_NET_NUMBER_OF_DESKTOPS";
    net_atom_names[WMDesktop] = "_NET_WM_DESKTOP";
    // Window-Types
    net_atom_names[WMWindowType] = "_NET_WM_WINDOW_TYPE";
    net_atom_names[WMWindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG";
    net_atom_names[WMWindowTypeNormal] = "_NET_WM_WINDOW_TYPE_NORMAL";
    net_atom_names[WMWindowTypeSplash] = "_NET_WM_WINDOW_TYPE_SPLASH";
    net_atom_names[WMWindowTypeUtility] = "_NET_WM_WINDOW_TYPE_UTILITY";
    XInternAtoms(d, net_atom_names, Net_N, False, net_atoms);
    // Indicate EWMH-Compliance
    const char wm_name[] = "xswm";
    const int wm_name_len = (int) strlen(wm_name);
    const Atom utf8string = XInternAtom(d, "UTF8_STRING", False);
    const Window wm_check = XCreateSimpleWindow(d, r, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(d, r, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, wm_check, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, wm_check, net_atoms[WMName], utf8string, 8,
        PropModeReplace, (unsigned char *) &wm_name, wm_name_len);
    XChangeProperty(d, r, net_atoms[Supported], XA_ATOM, 32,
        PropModeReplace, (unsigned char *) &net_atoms, Net_N);
    // EWMH-Configuration
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[CurrentDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0}, 1);
    XChangeProperty(d, r, net_atoms[DesktopGeometry], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {sw, sh}, 2);
    XChangeProperty(d, r, net_atoms[DesktopNames], utf8string, 8,
        PropModeReplace, (unsigned char *) "", 1);
    XChangeProperty(d, r, net_atoms[DesktopViewport], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0}, 2);
    XChangeProperty(d, r, net_atoms[NumberOfDesktops], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {1}, 1);
    XChangeProperty(d, r, net_atoms[WMName], utf8string, 8,
        PropModeReplace, (unsigned char *) &wm_name, wm_name_len);
    XChangeProperty(d, r, net_atoms[Workarea], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0, sw, sh}, 4);
    // WM configuration
    XSelectInput(d, r, SubstructureRedirectMask | SubstructureNotifyMask
        | StructureNotifyMask | PropertyChangeMask);
    XDefineCursor(d, r, XCreateFontCursor(d, 68));
    system("\"$XDG_CONFIG_HOME\"/xswm/autostart.sh &");
    // Scan for windows which started before xswm
    Window *windows = NULL;
    unsigned int windows_n;
    if (XQueryTree(d, r, &(Window) {None}, &(Window) {None}, &windows, &windows_n)
    && windows) {
        // Non-transient windows
        for (unsigned int i = 0; i < windows_n; i++) {
            XWindowAttributes wa;
            const Window w = windows[i];
            if (XGetWindowAttributes(d, w, &wa) && !wa.override_redirect
            && !XGetTransientForHint(d, w, &(Window) {None})
            && (wa.map_state == IsViewable || get_state(w) == IconicState))
                map_request(w);
        }
        // Transient windows
        for (unsigned int i = 0; i < windows_n; i++) {
            XWindowAttributes wa;
            const Window w = windows[i];
            if (XGetWindowAttributes(d, w, &wa)
            && XGetTransientForHint(d, w, &(Window) {None})
            && (wa.map_state == IsViewable || get_state(w) == IconicState))
                map_request(w);
        }
        XFree(windows);
    }
    // Main-Loop
    XEvent e;
    while (running) {
        XNextEvent(d, &e);
        switch (e.type) {
            case ButtonPress: button_press(&e.xbutton); break;
            case ClientMessage: client_message(&e.xclient); break;
            case ConfigureNotify: configure_notify(&e.xconfigure); break;
            case ConfigureRequest: configure_request(&e.xconfigurerequest); break;
            case FocusIn: focus_in(&e.xfocus); break;
            case MapRequest: map_request(e.xmaprequest.window); break;
            case PropertyNotify: property_notify(&e.xproperty); break;
            case UnmapNotify: unmap_notify(&e.xunmap); break;
        }
    }
    // Clean-Up
    XDestroyWindow(d, wm_check);
    while (head) {
        Client *old_head = head;
        head = head->next;
        free(old_head);
    }
    XCloseDisplay(d);
}
