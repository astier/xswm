#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>

// EWMH atoms
enum {
    ActiveWindow,
    ClientList,
    ClientListStacking,
    CloseWindow,
    CurrentDesktop,
    DesktopGeometry,
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
    Net_N
};

// ICCCM atoms
enum {
    DeleteWindow,
    Protocols,
    State,
    TakeFocus,
    WM_N
};

typedef struct Client {
    const Window w;
    Bool floating;
    int x, y;
    unsigned int width, height;
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

// X-Helpers
static Bool send_event(Window, Atom);
static int get_state(Window);
static int xerror(Display *, XErrorEvent *);
static void set_desktop_geometry(void);
static void set_frame_extents(Window);
static void set_state(Window, long);
static void update_client_list(Window, Bool);
static void update_client_list_stacking(void);

// Variables
static Atom wm_atoms[WM_N], net_atoms[Net_N], XA_WM_CMD;
static Bool running = True;
static Client *head; // Top-window and start of a linked-list
static Display *d;
static int clients_n = 0; // Number of clients in list
static unsigned int bw = 1; // border-width
static unsigned int sw, sh; // screen-width and -height
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
        set_frame_extents(w);
}

void configure_notify(const XConfigureEvent *e) {
    Client *c;
    const Window w = e->window;
    const unsigned int width  = (unsigned int) e->width;
    const unsigned int height = (unsigned int) e->height;
    if (w == r && (sw != width || sh != height)) {
        sh = height;
        sw = width;
        set_desktop_geometry();
        for (c = head; c; c = c->next)
            resize(c);
    } else if ((c = get_client(w))) {
        if ((unsigned int) e->border_width != bw)
            XSetWindowBorderWidth(d, w, bw);
        const int x = e->x, y = e->y;
        if (x != c->x || y != c->y || width != c->width || height != c->height) {
            c->x = x, c->y = y, c->width = width, c->height = height;
            resize(c);
        }
    }
}

void configure_request(const XConfigureRequestEvent *e) {
    const Window w = e->window;
    XWindowChanges wc;
    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.border_width = e->border_width;
    wc.sibling = e->above;
    wc.stack_mode = e->detail;
    XConfigureWindow(d, w, (unsigned int) e->value_mask, &wc);
}

// Prevent bad clients from stealing focus
void focus_in(const XFocusInEvent *e) {
    if (head && head->w != e->window)
        focus(head->w);
}

void map_request(const Window w) {
    if (get_client(w))
        return;
    // Initialize client and add to list
    memcpy(head = malloc(sizeof(Client)), &(Client) {w, False,
        (int) -bw, (int) -bw, sw, sh, head}, sizeof(Client));
    clients_n++;
    update_client_list(w, True);
    update_client_list_stacking();
    // Check if window is floating
    unsigned char *prop = NULL;
    if (XGetWindowProperty(d, w, net_atoms[WMWindowType], 0L, 1, False,
            XA_ATOM, &(Atom) {None}, &(int) {None}, &(unsigned long) {None},
            &(unsigned long) {None}, &prop) == Success) {
        Atom type;
        if (prop)
            type = *(Atom *) prop;
        else {
            if (XGetTransientForHint(d, w, &(Window) {None}))
                type = net_atoms[WMWindowTypeDialog];
            else
                type = net_atoms[WMWindowTypeNormal];
            XChangeProperty(d, w, net_atoms[WMWindowType], XA_ATOM, 32,
                PropModeReplace, (unsigned char *) &type, 1);
        }
        head->floating = type != net_atoms[WMWindowTypeNormal];
        if (prop)
            XFree(prop);
    }
    // Configure
    XGetGeometry(d, w, &(Window) {None}, &head->x, &head->y, &head->width,
        &head->height, &(unsigned int) {None}, &(unsigned int) {None});
    XChangeProperty(d, w, net_atoms[WMDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (int []) {0}, 1);
    XGrabButton(d, AnyButton, AnyModifier, w, True, ButtonPressMask,
        GrabModeSync, GrabModeSync, None, None);
    XSelectInput(d, w, FocusChangeMask);
    XSetWindowBorderWidth(d, w, (unsigned int) bw);
    set_frame_extents(w);
    resize(head);
    // Map and focus
    set_state(w, NormalState);
    XMapWindow(d, w);
    focus(w);
}

void property_notify(const XPropertyEvent *e) {
    if (e->window != r || e->atom != XA_WM_CMD)
        return;
    XTextProperty p;
    XGetTextProperty(d, r, &p, XA_WM_CMD);
    char cmd[16];
    strcpy(cmd, (char *) p.value);
    if      (!strcmp(cmd, "last"))  last();
    else if (!strcmp(cmd, "close")) close();
    else if (!strcmp(cmd, "quit"))  quit();
    XFree(p.value);
}

void unmap_notify(const XUnmapEvent *e) {
    const Window w = e->window;
    Client *c = get_client(w);
    if (!c)
        return;
    if (clients_n == 1)
        XChangeProperty(d, r, net_atoms[ActiveWindow],
            XA_WINDOW, 32, PropModeReplace, None, 0);
    if (c != head)
        get_parent(c)->next = c->next;
    else if (!head->next)
        head = NULL;
    else {
        head = head->next;
        focus(head->w);
    }
    free(c);
    clients_n--;
    update_client_list(w, False);
    update_client_list_stacking();
    set_state(w, WithdrawnState);
    XDeleteProperty(d, w, net_atoms[WMDesktop]);
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
    if (!send_event(w, wm_atoms[DeleteWindow]))
        XKillClient(d, w);
}

void focus(const Window w) {
    XSetInputFocus(d, w, RevertToPointerRoot, CurrentTime);
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW,
        32, PropModeReplace, (unsigned char *) &w, 1);
    send_event(w, wm_atoms[TakeFocus]);
}

void pop(const Window w) {
    Client *c = get_client(w);
    if (!c || head == c)
        return;
    focus(w);
    XRaiseWindow(d, w);
    // Update list
    get_parent(c)->next = c->next;
    c->next = head;
    head = c;
    update_client_list_stacking();
}

void resize(Client *c) {
    int x = (int) -bw, y = (int) -bw;
    unsigned int width = sw, height = sh;
    if (c->floating) {
        const unsigned int true_width = c->width  + bw * 2;
        if (true_width < sw) {
            x = (int) (sw - true_width) / 2;
            width = c->width;
        }
        const unsigned int true_height = c->height  + bw * 2;
        if (true_height < sh) {
            y = (int) (sh - true_height) / 2;
            height = c->height;
        }
    }
    c->x = x, c->y = y, c->width = width, c->height = height;
    XMoveResizeWindow(d, c->w, x, y, width, height);
}

Bool send_event(const Window w, const Atom protocol) {
    Atom *protocols;
    int count;
    if (!XGetWMProtocols(d, w, &protocols, &count))
        return False;
    Bool protocol_exists = False;
    while (!protocol_exists && count--)
        protocol_exists = protocols[count] == protocol;
    XFree(protocols);
    if (!protocol_exists)
        return False;
    XEvent e;
    e.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = wm_atoms[Protocols];
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long) protocol;
    e.xclient.data.l[1] = CurrentTime;
    return XSendEvent(d, w, False, NoEventMask, &e);
}

int get_state(const Window w) {
    int state = -1;
    unsigned char *prop;
    unsigned long nitems;
    if (XGetWindowProperty(d, w, wm_atoms[State], 0L, 2L, False, wm_atoms[State],
    &(Atom) {None}, &(int) {None}, &nitems, &(unsigned long) {None},
    (unsigned char **) &prop) == Success) {
        if (nitems > 0)
            state = *(int *) prop;
        XFree(prop);
    }
    return state;
}

int xerror(Display *dpy, XErrorEvent *e) { (void) dpy; (void) e; return 0; }

void set_desktop_geometry(void) {
    XChangeProperty(d, r, net_atoms[DesktopGeometry], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {sw, sh}, 2);
}

void set_frame_extents(const Window w) {
    XChangeProperty(d, w, net_atoms[FrameExtents], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {bw, bw, bw, bw}, 4);
}

void set_state(const Window w, const long state) {
    XChangeProperty(d, w, wm_atoms[State], wm_atoms[State], 32,
        PropModeReplace, (unsigned char *) (long []) {state, None}, 2);
}

void update_client_list(const Window w, const Bool add) {
    if (add) {
        XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
            PropModeAppend, (unsigned char *) &w, 1);
        return;
    }
    // Get _NET_CLIENT_LIST
    unsigned long nitems;
    unsigned char *prop = NULL;
    XGetWindowProperty(d, r, net_atoms[ClientList], 0, sizeof(Window), False, XA_WINDOW,
        &(Atom) {None}, &(int) {None}, &nitems, &(unsigned long) {None}, &prop);
    if (!prop)
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

int main(const int argc, const char *argv[]) {
    if (!(d = XOpenDisplay(NULL)))
        return EXIT_FAILURE;
    // Remote-Control by setting the XSWM_CMD property on the root-window
    // and catching it with property_notify()
    // Check property_notify() to see which commands are supported
    r = XDefaultRootWindow(d);
    XA_WM_CMD = XInternAtom(d, "XSWM_CMD", False);
    if (argc > 1) {
        XChangeProperty(d, r, XA_WM_CMD, XA_STRING, 8,
            PropModeReplace, (unsigned char *) argv[1], (int) strlen(argv[1]));
        XCloseDisplay(d);
        return EXIT_SUCCESS;
    }
    // Handle signals
    signal(SIGCHLD, SIG_IGN);
    // Fixes libreoffice-recovery-crash
    XSetErrorHandler(xerror);
    // Variables
    const int s = XDefaultScreen(d);
    sh = (unsigned int) XDisplayHeight(d, s);
    sw = (unsigned int) XDisplayWidth(d, s);
    // ICCCM atoms
    char *wm_atom_names[WM_N];
    wm_atom_names[DeleteWindow] = "WM_DELETE_WINDOW";
    wm_atom_names[Protocols] = "WM_PROTOCOLS";
    wm_atom_names[State] = "WM_STATE";
    wm_atom_names[TakeFocus] = "WM_TAKE_FOCUS";
    XInternAtoms(d, wm_atom_names, WM_N, False, wm_atoms);
    // EWMH atoms
    char *net_atom_names[Net_N];
    net_atom_names[ActiveWindow] = "_NET_ACTIVE_WINDOW";
    net_atom_names[ClientList] = "_NET_CLIENT_LIST";
    net_atom_names[ClientListStacking] = "_NET_CLIENT_LIST_STACKING";
    net_atom_names[CloseWindow] = "_NET_CLOSE_WINDOW";
    net_atom_names[CurrentDesktop] = "_NET_CURRENT_DESKTOP";
    net_atom_names[DesktopGeometry] = "_NET_DESKTOP_GEOMETRY";
    net_atom_names[DesktopViewport] = "_NET_DESKTOP_VIEWPORT";
    net_atom_names[FrameExtents] = "_NET_FRAME_EXTENTS";
    net_atom_names[NumberOfDesktops] = "_NET_NUMBER_OF_DESKTOPS";
    net_atom_names[RequestFrameExtents] = "_NET_REQUEST_FRAME_EXTENTS";
    net_atom_names[Supported] = "_NET_SUPPORTED";
    net_atom_names[SupportingWMCheck] = "_NET_SUPPORTING_WM_CHECK";
    net_atom_names[WMDesktop] = "_NET_WM_DESKTOP";
    net_atom_names[WMFullPlacement] = "_NET_WM_FULL_PLACEMENT";
    net_atom_names[WMName] = "_NET_WM_NAME";
    net_atom_names[WMWindowType] = "_NET_WM_WINDOW_TYPE";
    net_atom_names[WMWindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG";
    net_atom_names[WMWindowTypeNormal] = "_NET_WM_WINDOW_TYPE_NORMAL";
    net_atom_names[WMWindowTypeSplash] = "_NET_WM_WINDOW_TYPE_SPLASH";
    XInternAtoms(d, net_atom_names, Net_N, False, net_atoms);
    // EWMH configuration
    const Window wm_check = XCreateSimpleWindow(d, r, 0, 0, 1, 1, 0, 0, 0);
    const Atom utf8string = XInternAtom(d, "UTF8_STRING", False);
    const char wm_name[] = "xswm";
    XChangeProperty(d, wm_check, net_atoms[WMName], utf8string, 8,
        PropModeReplace, (unsigned char *) &wm_name, (int) strlen(wm_name));
    XChangeProperty(d, r, net_atoms[Supported], XA_ATOM, 32,
        PropModeReplace, (unsigned char *) &net_atoms, Net_N);
    XChangeProperty(d, r, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, wm_check, net_atoms[SupportingWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) &wm_check, 1);
    XChangeProperty(d, r, net_atoms[ActiveWindow], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientList], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[ClientListStacking], XA_WINDOW, 32,
        PropModeReplace, None, 0);
    XChangeProperty(d, r, net_atoms[CurrentDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0}, 1);
    XChangeProperty(d, r, net_atoms[DesktopViewport], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0}, 2);
    XChangeProperty(d, r, net_atoms[NumberOfDesktops], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {1}, 1);
    set_desktop_geometry();
    // WM configuration
    XSelectInput(d, r, SubstructureRedirectMask | SubstructureNotifyMask
        | StructureNotifyMask | PropertyChangeMask);
    XDefineCursor(d, r, XCreateFontCursor(d, 68));
    system("\"$XDG_CONFIG_HOME\"/xswm/autostart.sh &");
    // Scan for windows which started before xswm
    Window root, *children = NULL;
    unsigned int nchildren;
    if (XQueryTree(d, r, &root, &(Window) {None}, &children, &nchildren)) {
        XWindowAttributes wa;
        // Non-transient windows
        for (unsigned int i = 0; i < nchildren; i++) {
            Window w = children[i];
            if (XGetWindowAttributes(d, w, &wa)
            && !wa.override_redirect
            && !XGetTransientForHint(d, w, &root)
            && (wa.map_state == IsViewable || get_state(w) == IconicState))
                map_request(w);
        }
        // Transient windows
        for (unsigned int i = 0; i < nchildren; i++) {
            Window w = children[i];
            if (XGetWindowAttributes(d, w, &wa)
            && XGetTransientForHint(d, w, &root)
            && (wa.map_state == IsViewable || get_state(w) == IconicState))
                map_request(w);
        }
        XFree(children);
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
    XCloseDisplay(d);
}
