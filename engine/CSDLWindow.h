#ifndef CSDLWINDOW_H
#define CSDLWINDOW_H

#include <SDL3/SDL.h>
#include <list>
#include <string>
#include <functional>

#include "consts.h"
// #include "Controls/CDrawableObj.h"
// #include "Controls/CClickableObj.h"
// #include "Controls/CMsgBox.h"
// #include "Controls/CBanner.h"
// #include "Controls/CKeyboardObj.h"
// #include "Wrappers/CTTFWrapper.h"
#include "ClassTemplate/Singleton.h"
#include "ClassTemplate/TemplateCopyDisabled.h"

/// A wrapper of SDL_Window
/// Note that memory is managed manually
class CSDLWindow : public TemplateCopyDisabled
{
public:
	CSDLWindow(const char *, int, int, Uint32);

	/// @note Delete EVERY registered object before the destruction of this class !!
	virtual ~CSDLWindow();

	/// Set the icon of this window
	void SetIcon(SDL_Surface *, bool = true);

	/// Get the underlying pointer of this window
	SDL_Window *getWindow();
	/// Get the bound renderer
	SDL_Renderer *getRenderer();

	/// Create a new render of this window, replacing the old one
	void CreateRenderer(const char *name = "opengl");

	/// Get the list of all fonts
	/// The list consists of two fonts:
	/// 	1) default usages
	///		2) used for banners
	// std::list<CTTFWrapper *> * getFontsList();

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

	/// Register a new click-able object
	/// This method is called automatically when a new CClickableObj is constructed
	// void registerNewClickableObject(CClickableObj * obj);

	/// Register a new drawable object
	/// This method is called automatically when a new CDrawableObj is constructed
	/// The registered object is put at the end of the drawing queue defaultly
	/// which means newly registered object will be drawn lastly
	// void registerNewDrawableObject(CDrawableObj * obj);

	// void registerNewDrawableObject(CDrawableObj * obj,
	// 			std::list<CDrawableObj*>::const_iterator itr);

	/// Register a new object that response to keyboard events
	/// This method is called automatically when a new CKeyboardObj is constructed
	// void registerNewKeyboardObject(CKeyboardObj * obj);

	/// Lock the focus
	// void lockFocus(CKeyboardObj * obj);

	// CKeyboardObj * getCurrentFocus();

	/// Unlock the focus
	void unlockFocus();

	unsigned showMessageBoxWithChoice(std::list<std::string> &tar);

	// std::list<CClickableObj *> &getObjClickRespReg();
	// std::list<CDrawableObj *> &getObjDrawRespReg();
	// std::list<CKeyboardObj *> &getObjKeyRespReg();
	const int getHeight() const;
	const int getWidth() const;

	// /// Shows a Modal message box
	// /// It creates a new CMsgBox instance and changes the event logic of the current window
	// /// More specifically, it forces the focus to be fixed onto it
	// /// and blocks any mouse input, which is done by its constructor
	// /// Once an option is choose, the CMsgBox modifies some of its members,
	// /// but it will not be deleted, nor disappear,
	// /// and must be deleted explicitly.
	// /// Anything like restoring the event logic is done by its destructor,
	// /// and thus it will not disappear until deleted.
	// CMsgBox * showMessageBox(
	// 		const std::list<std::pair<std::string, char>> & choice);
	// CMsgBox * showMessageBox(std::list<std::pair<std::string, char>> && choice);

	// /// Create a banner
	// CBanner * showBanner(const std::string & str, int tick = 5000);

	// bool isBlockMouseEvent() const;
	// void setBlockMouseEvent(bool blockMouseEvent);
	// const std::list<std::function<bool(void)>>& getPostProcs() const;

	// bool isBlockKeyboardEvent() const;
	// void setBlockKeyboardEvent(bool blockKeyboardEvent);
	// bool isLockedFocus() const;
	// void setLockedFocus(bool lockedFocus);

	// CDrawableObj * getObjectByIdentifier(unsigned int id);

protected:
	bool blockMouseEvent;
	bool blockKeyboardEvent;

	bool lockedFocus;

	SDL_Window *window;
	SDL_Renderer *renderer;
	// std::list<CTTFWrapper *> fnts;

	// CKeyboardObj * keyFocus;

	const int width, height;

	// std::list<CClickableObj *> objClickRespReg;
	// std::list<CDrawableObj *> objDrawRespReg;
	// std::list<CKeyboardObj *> objKeyRespReg;

	std::list<std::function<bool(void)>> postProcs;

private:
};

#endif // CSDLWINDOW_H
