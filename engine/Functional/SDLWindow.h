#ifndef SDLWINDOW_H
#define SDLWINDOW_H

#include <SDL3/SDL.h>
#include <list>
#include <string>
#include <functional>

#include "consts.h"

namespace Engine
{
	/// A wrapper of SDL_Window
	/// Note that memory is managed manually
	class SDLWindow
	{
	public:
		SDLWindow(const char *, int, int, Uint32);

		/// @note Delete EVERY registered object before the destruction of this class !!
		virtual ~SDLWindow();

		/// Set the icon of this window
		void SetIcon(SDL_Surface *, bool = true);

		/// Get the underlying pointer of this window
		SDL_Window *getWindow();
		/// Get the bound renderer
		SDL_Renderer *getRenderer();

		/// Create a new render of this window, replacing the old one
		void CreateRenderer(const char *name = "opengl");

		/// Call this function in an event loop to process CLICK events
		/// @return TRUE if the event loop is to be continued
		virtual bool onClickOverall(SDL_Event &);

		/// Call this function in an event loop to re-draw all objects
		/// @return TRUE if the event loop is to be continued
		virtual bool onDrawOverall(bool = false);

		/// Call this function in an event loop to dispatch KEYDOWN and KEYUP events
		/// @return TRUE if the event loop is to be continued
		virtual bool onKeyOverall(SDL_Event &);

		/// Call this function in an event loop before processing any events
		/// @return TRUE if the event loop is to be continued
		virtual bool anteEventLoop();

		/// Call this function in an event loop after all events are processed
		/// @return TRUE if the event loop is to be continued
		virtual bool postEventLoop();

		/// Call this function to dispatch all events
		/// @return TRUE if the event loop is to be continued
		virtual bool dispatchEvents(SDL_Event &);

		const int getHeight() const;
		const int getWidth() const;

	protected:
		bool blockMouseEvent;
		bool blockKeyboardEvent;

		bool lockedFocus;

		SDL_Window *window;
		SDL_Renderer *renderer;
		SDL_GLContext glcontext;

		const int width, height;

		std::list<std::function<bool(void)>> postProcs;

	private:
	};
}
#endif // SDLWINDOW_H
