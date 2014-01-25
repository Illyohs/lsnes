#include "lua-base.hpp"
#include "lua-class.hpp"
#include "lua-function.hpp"
#include "lua-params.hpp"
#include "lua-pin.hpp"
#include "register-queue.hpp"
#include <iostream>
#include <cassert>

namespace lua
{
std::unordered_map<std::type_index, void*>& class_types()
{
	static std::unordered_map<std::type_index, void*> x;
	return x;
}
namespace
{
	int lua_trampoline_function(lua_State* L)
	{
		void* ptr = lua_touserdata(L, lua_upvalueindex(1));
		state* lstate = reinterpret_cast<state*>(lua_touserdata(L, lua_upvalueindex(2)));
		function* f = reinterpret_cast<function*>(ptr);
		state _L(*lstate, L);
		try {
			return f->invoke(_L);
		} catch(std::exception& e) {
			lua_pushfstring(L, "%s", e.what());
			lua_error(L);
		}
		return 0;
	}

	//Pushes given table to top of stack, creating if needed.
	void recursive_lookup_table(state& L, const std::string& tab)
	{
		if(tab == "") {
#if LUA_VERSION_NUM == 501
			L.pushvalue(LUA_GLOBALSINDEX);
#endif
#if LUA_VERSION_NUM == 502
			L.rawgeti(LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
#endif
			assert(L.type(-1) == LUA_TTABLE);
			return;
		}
		std::string u = tab;
		size_t split = u.find_last_of(".");
		std::string u1;
		std::string u2 = u;
		if(split < u.length()) {
			u1 = u.substr(0, split);
			u2 = u.substr(split + 1);
		}
		recursive_lookup_table(L, u1);
		L.getfield(-1, u2.c_str());
		if(L.type(-1) != LUA_TTABLE) {
			//Not a table, create a table.
			L.pop(1);
			L.newtable();
			L.setfield(-2, u2.c_str());
			L.getfield(-1, u2.c_str());
		}
		//Get rid of previous table.
		L.insert(-2);
		L.pop(1);
	}

	void register_function(state& L, const std::string& name, function* fun)
	{
		std::string u = name;
		size_t split = u.find_last_of(".");
		std::string u1;
		std::string u2 = u;
		if(split < u.length()) {
			u1 = u.substr(0, split);
			u2 = u.substr(split + 1);
		}
		recursive_lookup_table(L, u1);
		if(!fun)
			L.pushnil();
		else {
			void* ptr = reinterpret_cast<void*>(fun);
			L.pushlightuserdata(ptr);
			L.pushlightuserdata(&L);
			L.pushcclosure(lua_trampoline_function, 2);
		}
		L.setfield(-2, u2.c_str());
		L.pop(1);
	}
	typedef register_queue<state, function> regqueue_t;
	typedef register_queue<state::callback_proxy, state::callback_list> regqueue2_t;
	typedef register_queue<function_group, function> regqueue3_t;
}


state::state() throw(std::bad_alloc)
	: cbproxy(*this)
{
	master = NULL;
	lua_handle = NULL;
	oom_handler = builtin_oom;
	regqueue2_t::do_ready(cbproxy, true);
}

state::state(state& _master, lua_State* L)
	: cbproxy(*this)
{
	master = &_master;
	lua_handle = L;
}

state::~state() throw()
{
	if(master)
		return;
	for(auto i : function_groups)
		i.first->drop_callback(i.second);
	regqueue2_t::do_ready(cbproxy, false);
	if(lua_handle)
		lua_close(lua_handle);
}

void state::builtin_oom()
{
	std::cerr << "PANIC: FATAL: Out of memory" << std::endl;
	exit(1);
}

void* state::builtin_alloc(void* user, void* old, size_t olds, size_t news)
{
	if(news) {
		void* m = realloc(old, news);
		if(!m)
			reinterpret_cast<state*>(user)->oom_handler();
		return m;
	} else
		free(old);
	return NULL;
}


function::function(function_group& _group, const std::string& func) throw(std::bad_alloc)
	: group(_group)
{
	regqueue3_t::do_register(group, fname = func, *this);
}

function::~function() throw()
{
	regqueue3_t::do_unregister(group, fname);
}

void state::reset() throw(std::bad_alloc, std::runtime_error)
{
	if(master)
		return master->reset();
	if(lua_handle) {
		lua_State* tmp = lua_newstate(state::builtin_alloc, this);
		if(!tmp)
			throw std::runtime_error("Can't re-initialize Lua interpretter");
		lua_close(lua_handle);
		for(auto& i : callbacks)
			i.second->clear();
		lua_handle = tmp;
	} else {
		//Initialize new.
		lua_handle = lua_newstate(state::builtin_alloc, this);
		if(!lua_handle)
			throw std::runtime_error("Can't initialize Lua interpretter");
	}
	for(auto i : function_groups)
		i.first->request_callback([this](std::string name, function* func) -> void {
			register_function(*this, name, func);
		});
}

void state::deinit() throw()
{
	if(master)
		return master->deinit();
	if(lua_handle)
		lua_close(lua_handle);
	lua_handle = NULL;
}

void state::add_function_group(function_group& group)
{
	function_groups.insert(std::make_pair(&group, group.add_callback([this](const std::string& name,
		function* func) -> void {
		this->function_callback(name, func);
	}, [this](function_group* x) {
		for(auto i = this->function_groups.begin(); i != this->function_groups.end();)
			if(i->first == x)
				i = this->function_groups.erase(i);
			else
				i++;
	})));
}

void state::function_callback(const std::string& name, function* func)
{
	if(master)
		return master->function_callback(name, func);
	if(lua_handle)
		register_function(*this, name, func);
}

bool state::do_once(void* key)
{
	if(master)
		return master->do_once(key);
	pushlightuserdata(key);
	rawget(LUA_REGISTRYINDEX);
	if(type(-1) == LUA_TNIL) {
		pop(1);
		pushlightuserdata(key);
		pushlightuserdata(key);
		rawset(LUA_REGISTRYINDEX);
		return true;
	} else {
		pop(1);
		return false;
	}
}

state::callback_list::callback_list(state& _L, const std::string& _name, const std::string& fncbname)
	: L(_L), name(_name), fn_cbname(fncbname)
{
	regqueue2_t::do_register(L.cbproxy, name, *this);
}

state::callback_list::~callback_list()
{
	regqueue2_t::do_unregister(L.cbproxy, name);
	if(!L.handle())
		return;
	for(auto& i : callbacks) {
		L.pushlightuserdata(&i);
		L.pushnil();
		L.rawset(LUA_REGISTRYINDEX);
	}
}

void state::callback_list::_register(state& _L)
{
	callbacks.push_back(0);
	_L.pushlightuserdata(&*callbacks.rbegin());
	_L.pushvalue(-2);
	_L.rawset(LUA_REGISTRYINDEX);
}

void state::callback_list::_unregister(state& _L)
{
	for(auto i = callbacks.begin(); i != callbacks.end();) {
		_L.pushlightuserdata(&*i);
		_L.rawget(LUA_REGISTRYINDEX);
		if(_L.rawequal(-1, -2)) {
			char* key = &*i;
			_L.pushlightuserdata(key);
			_L.pushnil();
			_L.rawset(LUA_REGISTRYINDEX);
			i = callbacks.erase(i);
		} else
			i++;
		_L.pop(1);
	}
}

function_group::function_group()
{
	next_handle = 0;
	regqueue3_t::do_ready(*this, true);
}

function_group::~function_group()
{
	for(auto i : functions)
		for(auto j : callbacks)
			j.second(i.first, NULL);
	for(auto i : dcallbacks)
		i.second(this);
	regqueue3_t::do_ready(*this, false);
}

void function_group::request_callback(std::function<void(std::string, function*)> cb)
{
	for(auto i : functions)
		cb(i.first, i.second);
}

int function_group::add_callback(std::function<void(std::string, function*)> cb,
	std::function<void(function_group*)> dcb)
{
	int handle = next_handle++;
	callbacks[handle] = cb;
	dcallbacks[handle] = dcb;
	for(auto i : functions)
		cb(i.first, i.second);
	return handle;
}

void function_group::drop_callback(int handle)
{
	callbacks.erase(handle);
}

void function_group::do_register(const std::string& name, function& fun)
{
	functions[name] = &fun;
	for(auto i : callbacks)
		i.second(name, &fun);
}

void function_group::do_unregister(const std::string& name)
{
	functions.erase(name);
	for(auto i : callbacks)
		i.second(name, NULL);
}

std::list<class_ops>& userdata_recogn_fns()
{
	static std::list<class_ops> x;
	return x;
}

std::string try_recognize_userdata(state& state, int index)
{
	for(auto i : userdata_recogn_fns())
		if(i.is(state, index))
			return i.name();
	//Hack: Lua builtin file objects.
	state.pushstring("FILE*");
	state.rawget(LUA_REGISTRYINDEX);
	if(state.getmetatable(index)) {
		if(state.rawequal(-1, -2)) {
			state.pop(2);
			return "FILE*";
		}
		state.pop(1);
	}
	state.pop(1);
	return "unknown";
}

std::string try_print_userdata(state& L, int index)
{
	for(auto i : userdata_recogn_fns())
		if(i.is(L, index))
			return i.print(L, index);
	return "no data available";
}

int state::vararg_tag::pushargs(state& L)
{
	int e = 0;
	for(auto i : args) {
		if(i == "")
			L.pushnil();
		else if(i == "true")
			L.pushboolean(true);
		else if(i == "false")
			L.pushboolean(false);
		else if(regex_match("[+-]?(|0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?(0|[1-9][0-9]*))?", i))
			L.pushnumber(strtod(i.c_str(), NULL));
		else if(i[0] == ':')
			L.pushlstring(i.substr(1));
		else
			L.pushlstring(i);
		e++;
	}
	return e;
}
}
