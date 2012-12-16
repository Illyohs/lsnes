#include "core/audioapi.hpp"
#include "core/command.hpp"
#include "core/dispatch.hpp"
#include "core/framerate.hpp"
#include "lua/lua.hpp"
#include "core/misc.hpp"
#include "core/window.hpp"
#include "fonts/wrapper.hpp"
#include "library/framebuffer.hpp"
#include "library/string.hpp"
#include "library/minmax.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <deque>
#include <sys/time.h>
#include <unistd.h>
#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/iostreams/filter/symmetric.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

#define MAXMESSAGES 5000
#define INIT_WIN_SIZE 6

mutex::holder::holder(mutex& m) throw()
	: mut(m)
{
	mut.lock();
}

mutex::holder::~holder() throw()
{
	mut.unlock();
}

mutex::~mutex() throw()
{
}

mutex::mutex() throw()
{
}

condition::~condition() throw()
{
}

mutex& condition::associated() throw()
{
	return assoc;
}

condition::condition(mutex& m)
	: assoc(m)
{
}

thread_id::thread_id() throw()
{
}

thread_id::~thread_id() throw()
{
}

thread::thread() throw()
{
	alive = true;
	joined = false;
}

thread::~thread() throw()
{
}

bool thread::is_alive() throw()
{
	return alive;
}

void* thread::join() throw()
{
	if(!joined)
		this->_join();
	joined = true;
	return returns;
}

void thread::notify_quit(void* ret) throw()
{
	returns = ret;
	alive = false;
}

keypress::keypress()
{
	key1 = NULL;
	key2 = NULL;
	value = 0;
}

keypress::keypress(keyboard_modifier_set mod, keyboard_key& _key, short _value)
{
	modifiers = mod;
	key1 = &_key;
	key2 = NULL;
	value = _value;
}

keypress::keypress(keyboard_modifier_set mod, keyboard_key& _key, keyboard_key& _key2, short _value)
{
	modifiers = mod;
	key1 = &_key;
	key2 = &_key2;
	value = _value;
}

volatile bool platform::do_exit_dummy_event_loop = false;

namespace
{
	bool queue_function_run = false;

	function_ptr_command<> identify_key(lsnes_cmd, "show-plugins", "Show plugins in use",
		"Syntax: show-plugins\nShows plugins in use.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			messages << "Graphics:\t" << graphics_plugin::name << std::endl;
			messages << "Sound:\t" << audioapi_driver_name << std::endl;
			messages << "Joystick:\t" << joystick_plugin::name << std::endl;
		});

	function_ptr_command<const std::string&> enable_sound(lsnes_cmd, "enable-sound", "Enable/Disable sound",
		"Syntax: enable-sound <on/off>\nEnable or disable sound.\n",
		[](const std::string& args) throw(std::bad_alloc, std::runtime_error) {
			switch(string_to_bool(args)) {
			case 1:
				if(!audioapi_driver_initialized())
					throw std::runtime_error("Sound failed to initialize and is disabled");
				platform::sound_enable(true);
				break;
			case 0:
				if(audioapi_driver_initialized())
					platform::sound_enable(false);
				break;
			default:
				throw std::runtime_error("Bad sound setting");
			}
		});

	inverse_key ienable_sound("enable-sound on", "Sound‣Enable");
	inverse_key idisable_sound("enable-sound off", "Sound‣Disable");

	function_ptr_command<const std::string&> set_volume(lsnes_cmd, "set-volume", "Set sound volume",
		"Syntax: set-volume <scale>\nset-volume <scale>%\nset-volume <scale>dB\nSet sound volume\n",
		[](const std::string& value) throw(std::bad_alloc, std::runtime_error) {
			regex_results r;
			double parsed = 1;
			if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)", value))
				parsed = strtod(r[1].c_str(), NULL);
			else if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)%", value))
				parsed = strtod(r[1].c_str(), NULL) / 100;
			else if(r = regex("([+-]?([0-9]*.[0-9]+|[0-9]+))dB", value))
				parsed = pow(10, strtod(r[1].c_str(), NULL) / 20);
			else {
				messages << "Invalid volume" << std::endl;
				return;
			}
			audioapi_music_volume(parsed);
		});

	function_ptr_command<const std::string&> set_volume2(lsnes_cmd, "set-voice-volume", "Set voice playback "
		"volume", "Syntax: set-voice-volume <scale>\nset-voice-volume <scale>%\nset-voice-volume <scale>dB\n"
		"Set voice volume\n",
		[](const std::string& value) throw(std::bad_alloc, std::runtime_error) {
			regex_results r;
			double parsed = 1;
			if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)", value))
				parsed = strtod(r[1].c_str(), NULL);
			else if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)%", value))
				parsed = strtod(r[1].c_str(), NULL) / 100;
			else if(r = regex("([+-]?([0-9]*.[0-9]+|[0-9]+))dB", value))
				parsed = pow(10, strtod(r[1].c_str(), NULL) / 20);
			else {
				messages << "Invalid volume" << std::endl;
				return;
			}
			audioapi_voicep_volume(parsed);
		});

	function_ptr_command<const std::string&> set_volume3(lsnes_cmd, "set-record-volume", "Set voice record "
		"volume", "Syntax: set-record-volume <scale>\nset-record-volume <scale>%\nset-record-volume "
		"<scale>dB\nSet record volume\n",
		[](const std::string& value) throw(std::bad_alloc, std::runtime_error) {
			regex_results r;
			double parsed = 1;
			if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)", value))
				parsed = strtod(r[1].c_str(), NULL);
			else if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)%", value))
				parsed = strtod(r[1].c_str(), NULL) / 100;
			else if(r = regex("([+-]?([0-9]*.[0-9]+|[0-9]+))dB", value))
				parsed = pow(10, strtod(r[1].c_str(), NULL) / 20);
			else {
				messages << "Invalid volume" << std::endl;
				return;
			}
			audioapi_voicer_volume(parsed);
		});


	emulator_status emustatus;

	class window_output
	{
	public:
		typedef char char_type;
		typedef boost::iostreams::sink_tag category;
		window_output(int* dummy)
		{
		}

		void close()
		{
		}

		std::streamsize write(const char* s, std::streamsize n)
		{
			size_t oldsize = stream.size();
			stream.resize(oldsize + n);
			memcpy(&stream[oldsize], s, n);
			while(true) {
				size_t lf = stream.size();
				for(size_t i = 0; i < stream.size(); i++)
					if(stream[i] == '\n') {
						lf = i;
						break;
					}
				if(lf == stream.size())
					break;
				std::string foo(stream.begin(), stream.begin() + lf);
				platform::message(foo);
				if(lf + 1 < stream.size())
					memmove(&stream[0], &stream[lf + 1], stream.size() - lf - 1);
				stream.resize(stream.size() - lf - 1);
			}
			return n;
		}
	protected:
		std::vector<char> stream;
	};

	class msgcallback : public messagebuffer::update_handler
	{
	public:
		~msgcallback() throw() {};
		void messagebuffer_update() throw(std::bad_alloc, std::runtime_error)
		{
			platform::notify_message();
		}
	} msg_callback_obj;

	std::ofstream system_log;
	bool sounds_enabled = true;
}

emulator_status& platform::get_emustatus() throw()
{
	return emustatus;
}

void platform::sound_enable(bool enable) throw()
{
	audioapi_driver_enable(enable);
	sounds_enabled = enable;
	information_dispatch::do_sound_unmute(enable);
}

void platform::set_sound_device(const std::string& dev) throw()
{
	try {
		audioapi_driver_set_device(dev);
	} catch(std::exception& e) {
		out() << "Error changing sound device: " << e.what() << std::endl;
	}
	//After failed change, we don't know what is selected.
	information_dispatch::do_sound_change(audioapi_driver_get_device());
}

bool platform::is_sound_enabled() throw()
{
	return sounds_enabled;
}


void platform::init()
{
	do_exit_dummy_event_loop = false;
	msgbuf.register_handler(msg_callback_obj);
	system_log.open("lsnes.log", std::ios_base::out | std::ios_base::app);
	time_t curtime = time(NULL);
	struct tm* tm = localtime(&curtime);
	char buffer[1024];
	strftime(buffer, 1023, "%Y-%m-%d %H:%M:%S %Z", tm);
	system_log << "-----------------------------------------------------------------------" << std::endl;
	system_log << "lsnes started at " << buffer << std::endl;
	system_log << "-----------------------------------------------------------------------" << std::endl;
	do_init_font();
	graphics_plugin::init();
	audioapi_init();
	audioapi_driver_init();
	joystick_plugin::init();
}

void platform::quit()
{
	joystick_plugin::quit();
	audioapi_driver_quit();
	audioapi_quit();
	graphics_plugin::quit();
	msgbuf.unregister_handler(msg_callback_obj);
	time_t curtime = time(NULL);
	struct tm* tm = localtime(&curtime);
	char buffer[1024];
	strftime(buffer, 1023, "%Y-%m-%d %H:%M:%S %Z", tm);
	system_log << "-----------------------------------------------------------------------" << std::endl;
	system_log << "lsnes shutting down at " << buffer << std::endl;
	system_log << "-----------------------------------------------------------------------" << std::endl;
	system_log.close();
}

std::ostream& platform::out() throw(std::bad_alloc)
{
	static std::ostream* cached = NULL;
	int dummy;
	if(!cached)
		cached = new boost::iostreams::stream<window_output>(&dummy);
	return *cached;
}

messagebuffer platform::msgbuf(MAXMESSAGES, INIT_WIN_SIZE);


void platform::message(const std::string& msg) throw(std::bad_alloc)
{
	mutex::holder h(msgbuf_lock());
	std::string msg2 = msg;
	while(msg2 != "") {
		std::string forlog;
		extract_token(msg2, forlog, "\n");
		msgbuf.add_message(forlog);
		if(system_log)
			system_log << forlog << std::endl;
	}
}

void platform::fatal_error() throw()
{
	time_t curtime = time(NULL);
	struct tm* tm = localtime(&curtime);
	char buffer[1024];
	strftime(buffer, 1023, "%Y-%m-%d %H:%M:%S %Z", tm);
	system_log << "-----------------------------------------------------------------------" << std::endl;
	system_log << "lsnes paniced at " << buffer << std::endl;
	system_log << "-----------------------------------------------------------------------" << std::endl;
	system_log.close();
	graphics_plugin::fatal_error();
	exit(1);
}

namespace
{
	mutex* queue_lock;
	condition* queue_condition;
	std::deque<keypress> keypresses;
	std::deque<std::string> commands;
	std::deque<std::pair<void(*)(void*), void*>> functions;
	volatile bool normal_pause;
	volatile bool modal_pause;
	volatile uint64_t continue_time;
	volatile uint64_t next_function;
	volatile uint64_t functions_executed;

	void init_threading()
	{
		if(!queue_lock)
			queue_lock = &mutex::aquire();
		if(!queue_condition)
			queue_condition = &condition::aquire(*queue_lock);
	}

	void internal_run_queues(bool unlocked) throw()
	{
		init_threading();
		if(!unlocked)
			queue_lock->lock();
		try {
			//Flush keypresses.
			while(!keypresses.empty()) {
				keypress k = keypresses.front();
				keypresses.pop_front();
				queue_lock->unlock();
				if(k.key1)
					k.key1->set_state(k.modifiers, k.value);
				if(k.key2)
					k.key2->set_state(k.modifiers, k.value);
				queue_lock->lock();
				queue_function_run = true;
			}
			//Flush commands.
			while(!commands.empty()) {
				std::string c = commands.front();
				commands.pop_front();
				queue_lock->unlock();
				lsnes_cmd.invoke(c);
				queue_lock->lock();
				queue_function_run = true;
			}
			//Flush functions.
			while(!functions.empty()) {
				std::pair<void(*)(void*), void*> f = functions.front();
				functions.pop_front();
				queue_lock->unlock();
				f.first(f.second);
				queue_lock->lock();
				++functions_executed;
				queue_function_run = true;
			}
			queue_condition->signal();
		} catch(std::bad_alloc& e) {
			OOM_panic();
		} catch(std::exception& e) {
			std::cerr << "Fault inside platform::run_queues(): " << e.what() << std::endl;
			exit(1);
		}
		if(!unlocked)
			queue_lock->unlock();
	}

	uint64_t on_idle_time;
	uint64_t on_timer_time;
	void reload_lua_timers()
	{
		on_idle_time = lua_timed_hook(LUA_TIMED_HOOK_IDLE);
		on_timer_time = lua_timed_hook(LUA_TIMED_HOOK_TIMER);
		queue_function_run = false;
	}
}

#define MAXWAIT 100000ULL

void platform::dummy_event_loop() throw()
{
	init_threading();
	while(!do_exit_dummy_event_loop) {
		mutex::holder h(*queue_lock);
		internal_run_queues(true);
		queue_condition->wait(MAXWAIT);
	}
}

void platform::exit_dummy_event_loop() throw()
{
	init_threading();
	do_exit_dummy_event_loop = true;
	mutex::holder h(*queue_lock);
	queue_condition->signal();
	usleep(200000);
}

void platform::flush_command_queue() throw()
{
	reload_lua_timers();
	queue_function_run = false;
	if(modal_pause || normal_pause)
		freeze_time(get_utime());
	init_threading();
	bool run_idle = false;
	while(true) {
		uint64_t now = get_utime();
		if(now >= on_timer_time) {
			lua_callback_do_timer();
			reload_lua_timers();
		}
		if(run_idle) {
			lua_callback_do_idle();
			reload_lua_timers();
			run_idle = false;
		}
		mutex::holder h(*queue_lock);
		internal_run_queues(true);
		if(!pausing_allowed)
			break;
		if(queue_function_run)
			reload_lua_timers();
		now = get_utime();
		uint64_t waitleft = 0;
		waitleft = (now < continue_time) ? (continue_time - now) : 0;
		waitleft = (modal_pause || normal_pause) ? MAXWAIT : waitleft;
		waitleft = min(waitleft, static_cast<uint64_t>(MAXWAIT));
		if(waitleft > 0) {
			if(now >= on_idle_time) {
				run_idle = true;
				waitleft = 0;
			}
			if(on_idle_time >= now)
				waitleft = min(waitleft, on_idle_time - now);
			if(on_timer_time >= now)
				waitleft = min(waitleft, on_timer_time - now);
			if(waitleft > 0)
				queue_condition->wait(waitleft);
		} else
			break;
		//If we had to wait, check queues at least once more.
	}
	if(!modal_pause && !normal_pause)
		unfreeze_time(get_utime());
}

void platform::set_paused(bool enable) throw()
{
	normal_pause = enable;
}

void platform::wait(uint64_t usec) throw()
{
	reload_lua_timers();
	continue_time = get_utime() + usec;
	init_threading();
	bool run_idle = false;
	while(true) {
		uint64_t now = get_utime();
		if(now >= on_timer_time) {
			lua_callback_do_timer();
			reload_lua_timers();
		}
		if(run_idle) {
			lua_callback_do_idle();
			run_idle = false;
			reload_lua_timers();
		}
		mutex::holder h(*queue_lock);
		internal_run_queues(true);
		if(queue_function_run)
			reload_lua_timers();
		now = get_utime();
		uint64_t waitleft = 0;
		waitleft = (now < continue_time) ? (continue_time - now) : 0;
		waitleft = min(static_cast<uint64_t>(MAXWAIT), waitleft);
		if(waitleft > 0) {
			if(now >= on_idle_time) {
				run_idle = true;
				waitleft = 0;
			}
			if(on_idle_time >= now)
				waitleft = min(waitleft, on_idle_time - now);
			if(on_timer_time >= now)
				waitleft = min(waitleft, on_timer_time - now);
			if(waitleft > 0)
				queue_condition->wait(waitleft);
		} else
			return;
	}
}

void platform::cancel_wait() throw()
{
	init_threading();
	continue_time = 0;
	mutex::holder h(*queue_lock);
	queue_condition->signal();
}

void platform::set_modal_pause(bool enable) throw()
{
	modal_pause = enable;
}

void platform::queue(const keypress& k) throw(std::bad_alloc)
{
	init_threading();
	mutex::holder h(*queue_lock);
	keypresses.push_back(k);
	queue_condition->signal();
}

void platform::queue(const std::string& c) throw(std::bad_alloc)
{
	init_threading();
	mutex::holder h(*queue_lock);
	commands.push_back(c);
	queue_condition->signal();
}

void platform::queue(void (*f)(void* arg), void* arg, bool sync) throw(std::bad_alloc)
{
	if(sync && queue_synchronous_fn_warning)
		std::cerr << "WARNING: Synchronous queue in callback to UI, this may deadlock!" << std::endl;
	init_threading();
	mutex::holder h(*queue_lock);
	++next_function;
	functions.push_back(std::make_pair(f, arg));
	queue_condition->signal();
	if(sync)
		while(functions_executed < next_function)
			queue_condition->wait(10000);
}

void platform::run_queues() throw()
{
	internal_run_queues(false);
}

namespace
{
	mutex* _msgbuf_lock;
	framebuffer<false>* our_screen;

	struct painter_listener : public information_dispatch
	{
		painter_listener();
		void on_set_screen(framebuffer<false>& scr);
		void on_screen_update();
		void on_status_update();
	} x;

	painter_listener::painter_listener() : information_dispatch("painter-listener") {}

	void painter_listener::on_set_screen(framebuffer<false>& scr)
	{
		our_screen = &scr;
	}

	void painter_listener::on_screen_update()
	{
		graphics_plugin::notify_screen();
	}

	void painter_listener::on_status_update()
	{
		graphics_plugin::notify_status();
	}
}

mutex& platform::msgbuf_lock() throw()
{
	if(!_msgbuf_lock)
		try {
			_msgbuf_lock = &mutex::aquire();
		} catch(...) {
			OOM_panic();
		}
	return *_msgbuf_lock;
}

void platform::screen_set_palette(unsigned rshift, unsigned gshift, unsigned bshift) throw()
{
	if(!our_screen)
		return;
	if(our_screen->get_palette_r() == rshift &&
		our_screen->get_palette_g() == gshift &&
		our_screen->get_palette_b() == bshift)
		return;
	our_screen->set_palette(rshift, gshift, bshift);
	graphics_plugin::notify_screen();
}

modal_pause_holder::modal_pause_holder()
{
	platform::set_modal_pause(true);
}

modal_pause_holder::~modal_pause_holder()
{
	platform::set_modal_pause(false);
}

bool platform::pausing_allowed = true;
double platform::global_volume = 1.0;
volatile bool queue_synchronous_fn_warning;
