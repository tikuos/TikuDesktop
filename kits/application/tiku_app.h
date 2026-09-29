/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app.h - application lifecycle and public desktop services.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_APP_H_
#define TIKU_APP_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_event.h"

#define TIKU_APP_MAX 16

struct tiku_dl;
struct tiku_msg;

#include "tiku_window.h"

/*
 * The services an application draws THROUGH -- and the reason it never
 * needs the desktop's private state. Installed desktop apps always run
 * out of process, using the window session over the desk socket. The
 * descriptor also supports system/firmware compositions, but that does
 * not authorize loading installed application code into the desktop.
 */
typedef struct tiku_app_services {
    void *ctx;
    /** @brief Ask for a window.  @return its id, or 0. */
    uint32_t (*open)(void *ctx, const char *title, int w, int h);
    /** @brief The window's next frame, whole. */
    int (*frame)(void *ctx, uint32_t id, const uint32_t *px, int w, int h);
    /**
     * @brief The window's next frame as what was DRAWN, when it can be.
     *
     * Hand over both the list recorded while painting and the pixels it
     * produced; which one travels depends on the link and on whether the
     * list described the whole window, and can differ per frame.
     * Appended rather than replacing frame(), so an application built
     * against the older header still links and simply never gets the
     * cheap path.
     *
     * @return 1 when the window was handed over by either road.
     */
    int (*present)(void *ctx, uint32_t id, const struct tiku_dl *dl,
                   const uint32_t *px, int w, int h);
    /** @brief Publish the window's menus, as the plain data they are. */
    int (*menus)(void *ctx, uint32_t id, const tiku_menuset_t *set);
    /** @brief Give the window back. */
    void (*close)(void *ctx, uint32_t id);
    /**
     * @brief Ask for a window ROLE, not a position.
     *
     * An application does not know the screen, so it cannot centre
     * itself; it knows what kind of window it opened, and the host owns
     * where such a window goes.  ANNOUNCE is the R5 panel rule: centred,
     * in the upper part of the screen, fixed size.  Appended, like
     * present(): a host built against the older header leaves it NULL,
     * which an application must read as the host deciding everything.
     */
    int (*place)(void *ctx, uint32_t id, int role);
    /**
     * @brief The window's content is now @p w by @p h.
     *
     * For a window whose size follows something that can change under
     * it -- the About box refits when the user picks another face.  The
     * host keeps the corner where it is: a window that re-placed itself
     * would jump under the pointer.  Appended; NULL means the window
     * keeps the size it opened at.
     */
    int (*resize)(void *ctx, uint32_t id, int w, int h);
    /**
     * @brief Ask the SHELL to let the user pick a file, and answer later.
     *
     * The application says which question (TIKU_APP_PICK_OPEN, _SAVE or
     * _LOCAL_OPEN), where to start and, saving, a name to offer.  The shell
     * puts up its own panel over the namespace it holds, so a device's files
     * are pickable by an application that never heard of devices; LOCAL_OPEN
     * takes host files alone and answers with a local path.  It does not block:
     * the answer arrives at picked(), or never; an older host leaves it NULL.
     *
     * @return nonzero when the question was put, or carried out of process.
     */
    int (*pick)(void *ctx, uint32_t id, int mode, const char *start,
                const char *name);
    /**
     * @brief Bring this window of mine to the front.
     *
     * What an application knows that the host does not: which of its own
     * windows the person just asked for, by picking a file already open
     * or a row of a Window menu; without this its only answer is a second
     * window onto the same thing.  It moves a window in front of the
     * application's others and steals nothing from another application,
     * the gesture having been made here.  An older host leaves it NULL.
     */
    int (*raise_window)(void *ctx, uint32_t id);
    /**
     * @brief Let the PERSON resize this window of mine.
     *
     * Windows arrive fixed, because most of an application's windows are
     * panels whose layout is their size.  A window that can grow, an
     * editor's page, says so; from then on the runtime owns the corner
     * and the application hears TIKU_EVENT_RESIZE with the content's new
     * size, repaints at it and hands the next frame over as usual.  An
     * older host leaves it NULL, and such a window stays its opening size.
     */
    int (*sizeable)(void *ctx, uint32_t id);
    /**
     * @brief Put @p text on the DESK's clipboard.
     *
     * One clipboard for the desk, not one per application: what is cut
     * here can be pasted into another application's page or the shell's
     * own rename and Save-panel fields, and the other way round.  Text
     * only; the file clipboard is a different thing and stays one.  An
     * older host leaves it NULL, and an application then keeps its habit
     * of having no clipboard.
     */
    int (*clip_set)(void *ctx, const char *text);
    /**
     * @brief What the desk's clipboard holds: the host's own copy, never
     *        NULL, good until the application next returns to its loop.
     *
     * Answered from what the host already knows -- out of process the
     * shell keeps every session's copy in step as the clipboard moves,
     * so this never waits on a wire.  @p stamp, when not NULL, gets a
     * number that moves whenever the text does, so a caller can tell a
     * change without comparing up to TIKU_CLIPBOARD_LIMIT bytes.
     */
    const char *(*clip_text)(void *ctx, unsigned *stamp);

    /**
     * @brief The boards the shell has mounted, one name a line.
     *
     * A local copy the shell keeps warm, like the clipboard: an
     * application asks it while drawing a menu, and a service that
     * returns data must never wait on the wire.
     * @return the bytes written.
     */
    int (*board_names)(void *ctx, char *out, size_t max);

    /**
     * @brief The current device the desktop is on, empty for this
     *        machine.  An application with no board of its own defaults
     *        its work to this; it changes as board_said(BOARD_CURRENT).
     * @return the bytes written.
     */
    int (*current_device)(void *ctx, char *out, size_t max);

    /**
     * @brief What @p board says about itself: one "label<TAB>value" a
     *        line -- its processor, its speed, what memory it has left.
     *
     * Answered from what the shell KEPT, never from the wire, so a
     * window drawing it never waits; a stale answer sets the shell
     * asking and lands on a later frame.  Empty until the first one has.
     * @return the bytes written.
     */
    int (*device_stats)(void *ctx, const char *board, char *out,
                        size_t max);

    /**
     * @brief Run @p program on the board called @p board.
     *
     * The shell hands it to that board's interpreter and TELLS the
     * application what it prints, as board_said(); nothing is returned
     * here but whether the asking was taken.
     * @return nonzero when the run was started.
     */
    int (*board_run)(void *ctx, const char *board, const char *program);

    /** @brief Answer the running program's INPUT with @p text. */
    int (*board_input)(void *ctx, const char *text);

    /** @brief Break the running program, as Ctrl-C at its console does. */
    int (*board_stop)(void *ctx);

    /**
     * @brief Keep @p program on the board called @p board, under @p name.
     *
     * The board's interpreter saves it in a slot of that name (at most
     * seven characters) and shows it as /data/<name>.bas.  Told as
     * board_said(TIKU_APP_BOARD_SAVED, name), or FAILED.
     * @return nonzero when the saving was started.
     */
        int (*board_save)(void *ctx, const char *board, const char *name,
                      const char *program);

    /**
     * @brief The port @p board is on, into @p out ("/dev/ttyACM0").
     *
     * Warm, like the names: an application asks while drawing, and a
     * service that returns data must never wait on the wire.
     * @return the bytes written.
     */
        int (*board_port)(void *ctx, const char *board, char *out, size_t max);

    /**
     * @brief What @p board IS, as its die says ("nRF54LM20B"), into
     *        @p out.  Warm, like the port.
     * @return the bytes written.
     */
    int (*board_mcu)(void *ctx, const char *board, char *out, size_t max);

    /**
     * @brief Ask the shell to let @p board go, port and all.
     *
     * For a tool that must have the port to itself -- one that reflashes
     * the board, above all -- since two owners on one port corrupt each
     * other.  Told as board_said(TIKU_APP_BOARD_RELEASED); ask for the
     * port BEFORE releasing, because a board let go has none.
     * @return nonzero when the asking was taken.
     */
    /**
     * @brief Open @p path as a double click on it would: a folder as a
     *        window, a document in its program.
     *
     * Paths are interpreted in the desktop's namespace, also for an
     * out-of-process application. An older host may leave this NULL.
     * @return nonzero when opened, or the asynchronous request was sent.
     */
    int (*open_path)(void *ctx, const char *path);

    int (*board_release)(void *ctx, const char *board);

    /**
     * @brief Ask for it back, wherever it is now.
     *
     * A reflashed board returns on whatever node the host gives it next,
     * so this is by name and the shell finds the rest.  Told as
     * board_said(TIKU_APP_BOARD_RECLAIMED), or FAILED when it has not
     * come back yet -- asking again costs nothing.
     */
        int (*board_reclaim)(void *ctx, const char *board);

    /**
     * @brief Ask the shell to look for boards on this machine's ports.
     *
     * Not done unasked: opening every serial port writes to whatever is
     * on it, and those ports belong to other things until somebody says
     * otherwise.  So an application that offers boards offers this too.
     * What is found joins board_names(); told as
     * board_said(TIKU_APP_BOARD_LOOKED) with how many are mounted now.
     * @return nonzero when the asking was taken.
     */
    int (*board_find)(void *ctx);

    /**
     * @brief Name this application's own page of the desktop's help.
     *
     * Said once, at start: it opens nothing.  What it buys is a Help
     * row in the system menu while one of this application's windows is
     * in front, which opens the page named here.  @p page is a name
     * inside the help folder, "apps/bench.html" and the like.
     *
     * An older host leaves this NULL, so an application asks before it
     * calls.
     * @return nonzero when there is a help folder to read the page from.
     */
    int (*help_page)(void *ctx, const char *page);
    /** @brief Open a contextual help topic, optionally with a #section. */
    int (*help_open)(void *ctx, const char *topic);
    /** @brief Show a generated report in the HTML reader (body below 15 KB).
     * Call only following a user action; no files, scripts or browser are opened.
     * The host copies the body before returning. */
    int (*help_html)(void *ctx, const char *title, const char *html);
    /** @brief Explicit local document supplied at launch, or NULL.
     * Valid throughout start(); copy it if needed later. Never a remembered
     * session path. Opening a document must not execute its contents. */
    const char *startup_document;
    /** Update a window's title, requested content size, and size controls.
     * Zero dimensions leave its current size unchanged. */
    int (*configure)(void *ctx, uint32_t id, const char *title,
                     int width, int height, int resizable, int zoomable);
    /** Optional structured launch data, valid until start() returns. */
    const struct tiku_msg *startup_message;
    /** Send a structured request to a desktop-owned service. */
    int (*send_message)(void *ctx, const struct tiku_msg *message);
} tiku_app_services_t;

/** @brief Which question pick() asks. */
#define TIKU_APP_PICK_OPEN 0
#define TIKU_APP_PICK_SAVE 1
/* Local-only readers receive a host path, not a device namespace path. */
#define TIKU_APP_PICK_LOCAL_OPEN 2
/* A local FOLDER: the one chosen, or the one the panel shows, as a host
 * path.  An older host refuses the mode and never answers. */
#define TIKU_APP_PICK_LOCAL_FOLDER 3

/**
 * @brief Window roles for place().
 *
 * Both sit centred in the upper third at a fixed size; they differ in
 * whether the window is FURNITURE a person handles.  A card the program
 * holds up is taken down by the program; a panel a person opened from a
 * menu has to be closable by the person who opened it.
 */
enum {
    TIKU_APP_PLACE_ANNOUNCE = 1,  /* a card: no tab, no close */
    TIKU_APP_PLACE_PANEL    = 2   /* managed: wears its tab and close */
};

typedef struct {
    const char *id;                 /* stable machine-readable identity */
    const char *name;               /* label shown to the user          */
    int  (*start)(void **state, const tiku_app_services_t *services);
    void (*stop)(void *state);
    int  (*event)(void *state, const tiku_event_t *event);
    void (*tick)(void *state, int64_t now_us);
    /* A pick from the window's published menus, wherever the bar
     * lives.  @return nonzero when the application is done. */
    int  (*pick)(void *state, uint32_t window, int command);
    int  (*run)(int argc, char **argv); /* blocking compatibility entry */
    /**
     * @brief The path the person chose, for a pick() this window asked.
     *
     * Called from the shell's own loop rather than from inside pick(),
     * so an application must be able to hear it at any time and must not
     * assume it is still in the state it was in when it asked.  A
     * cancelled panel never calls this, the same silence the shell's own
     * askers get.  Appended: an application that never asks leaves it
     * NULL, and a host must check before calling.
     */
    void (*picked)(void *state, uint32_t window, const char *path);
    /**
     * @brief An event, and the WINDOW of this application it landed in.
     *
     * Every other call the host makes says which window it means;
     * event() alone did not, which is why an application could hold two
     * windows and not tell their keys apart.  A host calls this when it
     * is there and event() when it is not, so an application with one
     * window needs neither to know nor to change.
     *
     * @return nonzero when the application is done, as event() does.
     */
    int  (*event_in)(void *state, uint32_t window,
                     const tiku_event_t *event);
    /**
     * @brief The window has GONE -- the runtime took it, not the
     *        application.
     *
     * Closing is the runtime's gesture (a title bar, Cmd-W, the shell
     * shutting down), so an application learns about it rather than
     * decides it.  An application that holds several windows must hear
     * this or it will paint into one that is not there.
     *
     * Appended: an application with one window may leave it NULL, and a
     * host must check before calling.
     */
    void (*closed)(void *state, uint32_t window);

    /**
     * @brief What the program running on a board printed, a line at a
     *        time, and then how it ended.
     *
     * @param what TIKU_APP_BOARD_*; @p text is the line for LINE, and
     *             the reason (or empty) for the others.
     */
    void (*board_said)(void *state, int what, const char *text);
    /** Additional service replies, borrowed only for this callback. */
    void (*message)(void *state, const struct tiku_msg *message);
} tiku_app_descriptor_t;

/*
 * What a LOADABLE application exports, and the only symbol a loader
 * looks for.  The version lives in the symbol NAME so an incompatible
 * future export is refused by absence rather than by misreading; the
 * numbers inside catch a file built against a different toolkit.
 */
/* 3: board_port(), board_release() and board_reclaim(), for a tool that
 * must have a board's port to itself.
 * 2: the board services and board_said().  An application built for 1
 * has no field to put them in, so the loader refuses it rather than
 * reading past the end of a descriptor that is a field short. */
/* 4: explicit startup_document in the services supplied by the host. */
#define TIKU_APP_ABI    5u

/** @brief What board_said() is carrying. */
#define TIKU_APP_BOARD_LINE     0   /**< one line the program printed   */
#define TIKU_APP_BOARD_ENDED    1   /**< it finished by itself          */
#define TIKU_APP_BOARD_STOPPED  2   /**< a person stopped it            */
#define TIKU_APP_BOARD_FAILED   3   /**< the board would not take it    */
#define TIKU_APP_BOARD_ASKS     4   /**< at an INPUT; text is its prompt */
#define TIKU_APP_BOARD_SAVED    5   /**< kept on the board; text = name */
#define TIKU_APP_BOARD_RELEASED 6   /**< let go; text = the board       */
#define TIKU_APP_BOARD_RECLAIMED 7  /**< taken back; text = the board   */
#define TIKU_APP_BOARD_LOOKED   8   /**< looked; text = how many there are */
#define TIKU_APP_BOARD_RUNNING  9   /**< sent, and now running          */
#define TIKU_APP_BOARD_CURRENT  10  /**< the current device changed;
                                     *   text = its name, empty for host */
#define TIKU_APP_EXPORT "tiku_app_v1"

typedef struct {
    uint32_t                          abi;   /* TIKU_APP_ABI      */
    uint32_t                          size;  /* of the descriptor      */
    const tiku_app_descriptor_t *app;
} tiku_app_export_t;

/**
 * @brief Load the application in the shared object at @p path.
 *
 * @param err Why not, when the answer is NULL; never left unwritten.
 * @return the descriptor, owned by the loaded object, or NULL.
 */
const tiku_app_descriptor_t *tiku_app_load(const char *path,
                                                     char *err,
                                                     size_t max);

typedef struct {
    const tiku_app_descriptor_t *app[TIKU_APP_MAX];
    int                               count;
} tiku_app_registry_t;

typedef struct {
    const tiku_app_descriptor_t *descriptor;
    const tiku_app_services_t   *services;
    void                             *state;
    int                               running;
} tiku_app_instance_t;

void tiku_app_registry_init(tiku_app_registry_t *registry);

/* Add one descriptor.  Duplicate ids and incomplete descriptors are
 * rejected, so application identity remains unambiguous. */
int tiku_app_registry_add(tiku_app_registry_t *registry,
                               const tiku_app_descriptor_t *descriptor);

const tiku_app_descriptor_t *tiku_app_registry_find(
    const tiku_app_registry_t *registry, const char *id);

int tiku_app_registry_count(const tiku_app_registry_t *registry);
const tiku_app_descriptor_t *tiku_app_registry_at(
    const tiku_app_registry_t *registry, int index);

int tiku_app_instance_start(tiku_app_instance_t *instance,
                                 const tiku_app_descriptor_t *descriptor,
                                 const tiku_app_services_t *services);
/**
 * @brief Hand @p event to @p descriptor, saying which window it landed in.
 *
 * The one place that decides between event_in() and event(), so a host
 * asks rather than choosing: an application is entitled to the same
 * answer wherever it is running.
 *
 * @return nonzero when the application says it is done.
 */
int tiku_app_deliver_event(const tiku_app_descriptor_t *descriptor,
                                void *state, uint32_t window,
                                const tiku_event_t *event);

int tiku_app_instance_event(tiku_app_instance_t *instance,
                                 const tiku_event_t *event);
void tiku_app_instance_tick(tiku_app_instance_t *instance,
                                 int64_t now_us);
void tiku_app_instance_stop(tiku_app_instance_t *instance);

/** Run a registered command-style app through its compatibility entry. */
int tiku_app_run(const tiku_app_descriptor_t *descriptor,
                      int argc, char **argv);

/**
 * @brief The request that sets tray item @p id beside the Deskbar's clock:
 *        @p icon from the desk's icon set, a few characters of @p text after
 *        it, and @p tip; no icon and no text take the item away.  A click
 *        on it comes back to the descriptor's message() as TIKU_MSG_TRAYHIT.
 * @return the message for the caller to send and free, or NULL.
 */
struct tiku_msg *tiku_app_tray_message(const char *id, const char *icon,
                                       const char *text, const char *tip);

/** @brief The same request, sent through @p services' send_message.
 *  @return 1 when it went; 0 from a host that takes no such requests. */
int tiku_app_tray_set(const tiku_app_services_t *services, const char *id,
                      const char *icon, const char *text, const char *tip);

#endif /* TIKU_APP_H_ */
