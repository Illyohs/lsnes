#ifndef _window__hpp__included__
#define _window__hpp__included__

#include "SDL.h"
#include "render.hpp"
#include <string>
#include <map>
#include <list>
#include <stdexcept>

#define WINSTATE_NORMAL 0
#define WINSTATE_COMMAND 1
#define WINSTATE_MODAL 2
#define WINSTATE_IDENTIFY 3

class window_internal;
class window;

/**
 * This is a handle to graphics system. Note that creating multiple contexts produces undefined results.
 */
class window
{
public:
/**
 * Create a graphics system handle, initializing the graphics system.
 */
	window();

/**
 * Destroy a graphics system handle, shutting down the graphics system.
 */
	~window();

/**
 * Adds a messages to mesage queue to be shown.
 * 
 * parameter msg: The messages to add (split by '\n').
 * throws std::bad_alloc: Not enough memory.
 */
	void message(const std::string& msg) throw(std::bad_alloc);

/**
 * Get output stream printing into message queue.
 * 
 * Note that lines printed there should be terminated by '\n'.
 * 
 * returns: The output stream.
 * throws std::bad_alloc: Not enough memory.
 */
	std::ostream& out() throw(std::bad_alloc);

/**
 * Displays a modal message, not returning until the message is acknowledged. Keybindings are not available, but
 * should quit be generated somehow, modal message will be closed and command callback triggered.
 * 
 * parameter msg: The message to show.
 * parameter confirm: If true, ask for Ok/cancel type input.
 * returns: If confirm is true, true if ok was chosen, false if cancel was chosen. Otherwise always false.
 * throws std::bad_alloc: Not enough memory.
 */
	bool modal_message(const std::string& msg, bool confirm = false) throw(std::bad_alloc);

/**
 * Displays fatal error message, quitting after the user acks it.
 */
	void fatal_error() throw();

/**
 * Bind a key.
 * 
 * parameter mod: Set of modifiers.
 * parameter modmask: Modifier mask (set of modifiers).
 * parameter keyname: Name of key or pseudo-key.
 * parameter command: Command to run.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Invalid key or modifier name, or conflict.
 */
	void bind(std::string mod, std::string modmask, std::string keyname, std::string command)
		throw(std::bad_alloc, std::runtime_error);

/**
 * Unbind a key.
 * 
 * parameter mod: Set of modifiers.
 * parameter modmask: Modifier mask (set of modifiers).
 * parameter keyname: Name of key or pseudo-key.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Invalid key or modifier name, or not bound.
 */
	void unbind(std::string mod, std::string modmask, std::string keyname) throw(std::bad_alloc,
		std::runtime_error);

/**
 * Dump bindings into this window.
 * 
 * throws std::bad_alloc: Not enough memory.
 */
	void dumpbindings() throw(std::bad_alloc);

/**
 * Processes inputs. If in non-modal mode (normal mode without pause), this returns quickly. Otherwise it waits
 * for modal mode to exit.
 * 
 * throws std::bad_alloc: Not enough memory.
 */
	void poll_inputs() throw(std::bad_alloc);

/**
 * Get emulator status area
 * 
 * returns: Emulator status area.
 */
	std::map<std::string, std::string>& get_emustatus() throw();

/**
 * Notify that the screen has been updated.
 * 
 * parameter full: Do full refresh if true.
 */
	void notify_screen_update(bool full = false) throw();

/**
 * Set the screen to use as main surface.
 * 
 * parameter scr: The screen to use.
 */
	void set_main_surface(screen& scr) throw();

/**
 * Enable/Disable pause mode.
 * 
 * parameter enable: Enable pause if true, disable otherwise.
 */
	void paused(bool enable) throw();

/**
 * Wait specified number of milliseconds (polling for input).
 * 
 * parameter msec: Number of ms to wait.
 * throws std::bad_alloc: Not enough memory.
 */
	void wait_msec(uint64_t msec) throw(std::bad_alloc);

/**
 * Cancel pending wait_msec, making it return now.
 */
	void cancel_wait() throw();

/**
 * Enable or disable sound.
 * 
 * parameter enable: Enable sounds if true, otherwise disable sounds.
 */
	void sound_enable(bool enable) throw();

/**
 * Input audio sample (at 32040.5Hz).
 * 
 * parameter left: Left sample.
 * parameter right: Right sample.
 */
	void play_audio_sample(uint16_t left, uint16_t right) throw();


/**
 * Set window main screen compensation parameters. This is used for mouse click reporting.
 * 
 * parameter xoffset: X coordinate of origin.
 * parameter yoffset: Y coordinate of origin.
 * parameter hscl: Horizontal scaling factor.
 * parameter vscl: Vertical scaling factor. 
 */
	void set_window_compensation(uint32_t xoffset, uint32_t yoffset, uint32_t hscl, uint32_t vscl);
private:
	window_internal* i;
	window(const window&);
	window& operator==(const window&);
};

/**
 * Get number of msec since some undetermined epoch.
 * 
 * returns: The number of milliseconds.
 */
uint64_t get_ticks_msec() throw();

#endif
