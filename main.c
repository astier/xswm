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
    DesktopViewport,
    FrameExtents,
    NumberOfDesktops,
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
    const Bool floating;
    int x, y, width, height;
    struct Client *next;
} Client;

// Functions
static Client * get_client(Window);
static Client * get_parent(const Client *);
static int xerror(Display *, XErrorEvent *);
static void add(Window);
static void delete(Window);
static void focus(Window);
static void pop(Window);
static void resize(Client *);
static void send_event(Window, Atom);
static void set_state(Window, long);
static void update_client_list(Window, Bool);
static void update_client_list_stacking(void);

// Event-Handlers
static void button_press(const XButtonPressedEvent *);
static void client_message(const XClientMessageEvent *);
static void configure_notify(const XConfigureEvent *);
static void configure_request(const XConfigureRequestEvent *);
static void focus_in(const XFocusInEvent *);
static void property_notify(const XPropertyEvent *);
static void unmap_notify(const XUnmapEvent *);

// Remote-Commands
static void close(void);
static void last(void);
static void quit(void);

// Variables
static Atom wm_atoms[WM_N], net_atoms[Net_N], XA_WM_CMD;
static Bool running = True;
static Display *d;
static int sh, sw; // screen-width and -height
static int gap_x, gap_y; // Minimum gap for floating-windows
static double gap_ratio = 0.05; // Used to compute gaps
static int bw = 1; // border-width
static Window r; // root-window

// Linked-List
static Client *head; // Top-window and start of a linked-list
static int clients_n = 0; // Number of clients in list

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

int xerror(Display *dpy, XErrorEvent *e) { (void) dpy; (void) e; return 0; }

void add(const Window w) {
    if (get_client(w))
        return;
    // Check if window is floating
    unsigned char *prop = NULL;
    XGetWindowProperty(d, w, net_atoms[WMWindowType], 0L, 1, False, XA_ATOM, &(Atom) {None},
        &(int) {None}, &(unsigned long) {None}, &(unsigned long) {None}, &prop);
    const Atom type = prop ? *(Atom *)prop : None;
    const Bool floating = type == None || type == net_atoms[WMWindowTypeNormal]
        ? False : True;
    XFree(prop);
    // Initialize client and add to list
    memcpy(head = malloc(sizeof(Client)), &(Client) {w, floating,
        None, None, None, None, head}, sizeof(Client));
    clients_n++;
    update_client_list(w, True);
    update_client_list_stacking();
    // Configure, map and focus window
    XChangeProperty(d, w, net_atoms[WMDesktop], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (int []) {0}, 1);
    XChangeProperty(d, w, net_atoms[FrameExtents], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *) (long []) {0, 0, 0, 0}, 4);
    XGrabButton(d, AnyButton, AnyModifier, w, True, ButtonPressMask,
        GrabModeSync, GrabModeSync, None, None);
    XSelectInput(d, w, FocusChangeMask);
    XSetWindowBorderWidth(d, w, (unsigned int) bw);
    resize(head);
    set_state(w, NormalState);
    XMapWindow(d, w);
    focus(w);
}

void delete(const Window w) { send_event(w, wm_atoms[DeleteWindow]); }

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
    // Initialize geometry for maximized windows
    int x = -bw, y = -bw; // Hide border
    unsigned int width  = (unsigned int) sw;
    unsigned int height = (unsigned int) sh;
    // Compute geometry for floating windows
    if (c->floating) {
        XGetGeometry(d, c->w, &(Window) {None}, &(int) {None}, &(int) {None},
            &width, &height, &(unsigned int) {None}, &(unsigned int) {None});
        // Center window
        const int bw2 = bw * 2;
        x = (sw - ((int) width  + bw2)) / 2;
        y = (sh - ((int) height + bw2)) / 2;
        // Make sure window is not to big
        if (x < gap_x) {
            x = gap_x;
            width = (unsigned int) (sw - bw2 - gap_x * 2);
        }
        if (y < gap_y) {
            y = gap_y;
            height = (unsigned int) (sh - bw2 - gap_y * 2);
        }
    }
    c->x = x, c->y = y, c->width = (int) width, c->height = (int) height;
    XMoveResizeWindow(d, c->w, x, y, width, height);
}

void send_event(const Window w, const Atom protocol) {
    Atom *protocols;
    int n;
    if (!XGetWMProtocols(d, w, &protocols, &n))
        return;
    Bool protocol_exists = False;
    while (!protocol_exists && n--)
        protocol_exists = protocols[n] == protocol;
    XFree(protocols);
    if (!protocol_exists)
        return;
    XEvent e;
    e.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = wm_atoms[Protocols];
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long) protocol;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, w, False, NoEventMask, &e);
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
    XGetWindowProperty(d, r, net_atoms[ClientList],
        0, sizeof(Window), False, XA_WINDOW, &(Atom) {None},
        &(int) {None}, &nitems, &(unsigned long) {None}, &prop);
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
}

void configure_notify(const XConfigureEvent *e) {
    Client *c;
    const Window w = e->window;
    const int x = e->x, y = e->y, wh = e->height, ww = e->width;
    if (w == r && (wh != sh || ww != sw)) {
        sh = wh;
        sw = ww;
        gap_x = (int) (sw * gap_ratio);
        gap_y = (int) (sh * gap_ratio);
        for (c = head; c; c = c->next)
            resize(c);
    } else if ((c = get_client(w))) {
        if (e->border_width != bw)
            XSetWindowBorderWidth(d, w, (unsigned int) bw);
        if (x != c->x || y != c->y || ww != c->width || wh != c->height)
            resize(c);
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

void close(void) { if (clients_n > 0) delete(head->w); }

void last(void) { if (clients_n > 1) pop(head->next->w); }

void quit(void) { running = False; }

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
    sh = XDisplayHeight(d, s);
    sw = XDisplayWidth(d, s);
    gap_x = (int) (sw * gap_ratio);
    gap_y = (int) (sh * gap_ratio);
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
    net_atom_names[DesktopViewport] = "_NET_DESKTOP_VIEWPORT";
    net_atom_names[FrameExtents] = "_NET_FRAME_EXTENTS";
    net_atom_names[NumberOfDesktops] = "_NET_NUMBER_OF_DESKTOPS";
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
    // WM configuration
    XSelectInput(d, r, SubstructureRedirectMask | SubstructureNotifyMask
        | StructureNotifyMask | PropertyChangeMask);
    XDefineCursor(d, r, XCreateFontCursor(d, 68));
    system("\"$XDG_CONFIG_HOME\"/xswm/autostart.sh &");
    // Scan for windows at startup
    Window *wins;
    unsigned int n;
    XQueryTree(d, r, &(Window) {None}, &(Window) {None}, &wins, &n);
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(d, wins[i], &wa)
        && !wa.override_redirect && wa.map_state == IsViewable)
            add(wins[i]);
    }
    if (wins)
        XFree(wins);
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
            case MapRequest: add(e.xmaprequest.window); break;
            case PropertyNotify: property_notify(&e.xproperty); break;
            case UnmapNotify: unmap_notify(&e.xunmap); break;
        }
    }
    // Clean-Up
    XCloseDisplay(d);
}
