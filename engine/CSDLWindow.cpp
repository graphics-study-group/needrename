#include "CSDLWindow.h"
#include "CException/exception.h"

CSDLWindow::CSDLWindow(const char * title, int w, int h,
		Uint32 flags) :
		width(w), height(h) {
	//ctor

	this->blockMouseEvent = false;
	this->blockKeyboardEvent = false;

	// this->lockedFocus = false;
	// this->keyFocus = nullptr;
	this->window = SDL_CreateWindow(title, w, h, flags);
	if (this->window == nullptr)
		throw Exception::SDLExceptions::cant_create_window();
	this->renderer = nullptr;
}

CSDLWindow::~CSDLWindow() {
	//dtor
	// while (!fnts.empty()) {
	// 	delete fnts.front();
	// 	fnts.pop_front();
	// }
	if (this->renderer != nullptr)
		SDL_DestroyRenderer(this->renderer);
	SDL_DestroyWindow(this->window);

	// Delete all objects, note that do not delete them again
	// for (CDrawableObj * p : this->objDrawRespReg)
	// 	delete p;

	// if (!this->objClickRespReg.empty()) {
	// 	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Objects are not deleted!");
	// 	for (auto item : this->objClickRespReg)
	// 		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "\tID:%u",
	// 				item->getIdentifier());
	// }

	// if (!this->objKeyRespReg.empty()) {
	// 	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Objects are not deleted!");
	// 	for (auto item : this->objKeyRespReg)
	// 		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "\tID:%u",
	// 				item->getIdentifier());
	// }
}

SDL_Window * CSDLWindow::getWindow() {
	return this->window;
}

SDL_Renderer * CSDLWindow::getRenderer() {
	return this->renderer;
}

void CSDLWindow::CreateRenderer(const char *name) {
	if (this->renderer != nullptr)
		SDL_DestroyRenderer(this->renderer);
	this->renderer = SDL_CreateRenderer(this->window, name);
	if (this->renderer == nullptr)
		throw Exception::SDLExceptions::cant_create_renderer();
}

// std::list<CTTFWrapper *> * CSDLWindow::getFontsList() {
// 	return &(this->fnts);
// }

void CSDLWindow::SetIcon(SDL_Surface * surface, bool freeSurface) {
	SDL_SetWindowIcon(this->window, surface);
	if (freeSurface)
		SDL_DestroySurface(surface);
}

bool CSDLWindow::onClickOverall(SDL_Event & event) {
	SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Clicking (%d, %d)",
			event.button.x, event.button.y);
// 	for (CClickableObj * obj : this->objClickRespReg) {
// // TODO (hp#1#): Add click event response
// 		SDL_Rect * respArea = obj->getRespArea();
// 		SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
// 				"Object %u, respArea (x:%d, y:%d, h:%d, w:%d)", obj->getIdentifier(),
// 				respArea->x, respArea->y, respArea->h, respArea->w);
// 		if (event.button.x >= respArea->x && event.button.y >= respArea->y
// 				&& event.button.x <= respArea->x + respArea->w
// 				&& event.button.y <= respArea->y + respArea->h) {
// 			bool ret = true;
// 			if (event.button.state == SDL_PRESSED) {
// 				if (event.button.button == SDL_BUTTON_LEFT)
// 					ret = obj->onClick(event);
// 				else if (event.button.button == SDL_BUTTON_RIGHT)
// 					ret = obj->onClickRight(event);
// 			}
// 			if (!lockedFocus)
// 				for (CKeyboardObj * newFocus : this->objKeyRespReg)
// 					if (obj->getIdentifier() == newFocus->getIdentifier()) {
// 						this->keyFocus = newFocus;
// 						break;
// 					}
// 			return ret;
// 		}
// 	}
	return true;
}

bool CSDLWindow::onDrawOverall(bool forcedRedraw) {
	//SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Redrawing");

	if (this->renderer == nullptr) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"onDrawOverall() is called but no renderer is available");
		return true;
	}

	// if this window need redrawing
	bool needRedraw = forcedRedraw;
	if (!forcedRedraw) {
		// TODO: draw !!!!
		// for (CDrawableObj * obj : objDrawRespReg)
		// 	if (obj->inquireRedraw()) {
		// 		SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
		// 				"Object %u needs redrawing", obj->getIdentifier());
		// 		needRedraw = true;
		// 		break;
		// 	}
	} else
		SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Forced redraw commenced");

	// do not redraw if it's not needed
	if (!needRedraw)
		return true;

	SDL_SetRenderDrawColor(this->renderer, 0xff, 0xff, 0xff, 0xff);
	SDL_RenderClear(this->renderer);

	// TODO: draw !!!!
	// SDL_Surface * pNewSurface;

	// pNewSurface = SDL_CreateRGBSurface(0, this->width, this->height, 32, RMASK,
	// GMASK, BMASK, AMASK);

	// if (pNewSurface == NULL)
	// 	throw Exception::SDLExceptions::cant_create_surface();

	// // redraw every object
	// for (CDrawableObj * obj : this->objDrawRespReg) {
	// 	if (/*forcedRedraw || obj->inquireRedraw()*/true) {
	// 		SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
	// 				"Object Redrawn, id: %d", obj->getIdentifier());

	// 		SurfaceWithDest * toDraw;
	// 		toDraw = obj->onRedraw();
	// 		// copy newly drawn surface to current one
	// 		SDL_BlitSurface(toDraw->surface, NULL, pNewSurface, &toDraw->dest);

	// 		// !Do not free the surface!
	// 		// SDL_FreeSurface(toDraw.surface);
	// 		//obj->requireRedraw(false);
	// 		delete toDraw;
	// 		if (obj->isOverridingOther())
	// 			break;
	// 	}
	// }

	// // present current surface
	// SDL_Texture * pNewTexture = SDL_CreateTextureFromSurface(this->renderer,
	// 		pNewSurface);
	// SDL_RenderCopy(this->renderer, pNewTexture, NULL, NULL);
	// SDL_RenderPresent(this->renderer);
	// // destroy current surface and texture
	// SDL_FreeSurface(pNewSurface);
	// SDL_DestroyTexture(pNewTexture);

	return true;
}

bool CSDLWindow::onKeyOverall(SDL_Event & event) {
	// if (keyFocus == nullptr) {
	// 	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
	// 			"A keyboard event is sent but no focus is set");
	// 	return true;
	// }
	// dispatch key events to focused object

	// TODO: key !!!!
	// if (event.type == SDL_KEYDOWN)
	// 	return keyFocus->onKeydown(event);
	// else if (event.type == SDL_KEYUP)
	// 	return keyFocus->onKeyup(event);

	SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"An event which is not a keyboard event is passed into onKeyOverall as a parameter");
	return true;
}

bool CSDLWindow::dispatchEvents(SDL_Event & event) {
	//SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Dispatching an event : %u\n", event.type);
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		if (this->blockKeyboardEvent)
			break;
		return this->onKeyOverall(event);
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (this->blockMouseEvent)
			break;
		return this->onClickOverall(event);
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_SHOWN:
		SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
				"Redrawn due to a window event");
		return this->onDrawOverall(true);
		break;
	default:
		break;
	}
	return true;
}

// void CSDLWindow::registerNewClickableObject(CClickableObj * obj) {
// // TODO (hp#1#): Check if any overlapped rectangles exist
// 	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
// 			"A CLIOBJ is registered: %s, %u", typeid(obj).name(),
// 			obj->getIdentifier());
// 	this->objClickRespReg.insert(this->objClickRespReg.end(), obj);
// }

// void CSDLWindow::registerNewDrawableObject(CDrawableObj * obj) {
// 	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
// 			"A DRAWOBJ is registered: %s, %u", typeid(obj).name(),
// 			obj->getIdentifier());
// 	this->objDrawRespReg.insert(this->objDrawRespReg.end(), obj);
// }

// void CSDLWindow::registerNewDrawableObject(CDrawableObj * obj,
// 		std::list<CDrawableObj*>::const_iterator itr) {
// 	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
// 			"A DRAWOBJ is registered: %s, %u", typeid(obj).name(),
// 			obj->getIdentifier());
// 	this->objDrawRespReg.insert(itr, obj);
// }

// void CSDLWindow::registerNewKeyboardObject(CKeyboardObj * obj) {
// 	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
// 			"A KBDOBJ is registered: %s, %u", typeid(obj).name(),
// 			obj->getIdentifier());
// 	this->objKeyRespReg.insert(this->objKeyRespReg.end(), obj);
// }

bool CSDLWindow::anteEventLoop() {
	this->onDrawOverall();

	// for (auto itr = this->objDrawRespReg.begin();
	// 		itr != this->objDrawRespReg.end();) {
	// 	if ((*itr)->isDestructing()) {
	// 		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
	// 				"Destroying object, id %d", (*itr)->getIdentifier());
	// 		delete *itr;
	// 		itr = this->objDrawRespReg.erase(itr);

	// 		// Do a forced redraw
	// 		this->onDrawOverall(true);
	// 	} else
	// 		itr++;
	// }

	return true;
}

bool CSDLWindow::postEventLoop() {
	bool ret = true;

	for (auto item : this->postProcs)
		if (item() == false) {
			ret = false;
			break;
		}

	return ret;
}

// void CSDLWindow::lockFocus(CKeyboardObj* obj) {

// 	for (CKeyboardObj * kobj : objKeyRespReg)
// 		if (kobj->getIdentifier() == obj->getIdentifier()) {
// 			lockedFocus = true;
// 			break;
// 		}

// 	if (!lockedFocus) {
// 		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
// 				"Focus is set to an unregistered object");
// 		lockedFocus = true;
// 	}
// 	keyFocus = obj;
// }

// CKeyboardObj * CSDLWindow::getCurrentFocus() {
// 	return this->keyFocus;
// }

// void CSDLWindow::unlockFocus() {
// 	lockedFocus = false;
// }

// std::list<CClickableObj*>& CSDLWindow::getObjClickRespReg() {
// 	return objClickRespReg;
// }

// std::list<CDrawableObj*>& CSDLWindow::getObjDrawRespReg() {
// 	return objDrawRespReg;
// }

// std::list<CKeyboardObj*>& CSDLWindow::getObjKeyRespReg() {
// 	return objKeyRespReg;
// }

const int CSDLWindow::getHeight() const {
	return height;
}

const int CSDLWindow::getWidth() const {
	return width;
}

// CMsgBox * CSDLWindow::showMessageBox(
// 		const std::list<std::pair<std::string, char>> & choice) {
// 	return NULL;
// }

// CMsgBox * CSDLWindow::showMessageBox(
// 		std::list<std::pair<std::string, char>> && choice) {
// 	return NULL;
// }

// bool CSDLWindow::isBlockMouseEvent() const {
// 	return blockMouseEvent;
// }

// void CSDLWindow::setBlockMouseEvent(bool blockMouseEvent) {
// 	this->blockMouseEvent = blockMouseEvent;
// }

// const std::list<std::function<bool(void)>>& CSDLWindow::getPostProcs() const {
// 	return postProcs;
// }

// bool CSDLWindow::isBlockKeyboardEvent() const {
// 	return blockKeyboardEvent;
// }

// void CSDLWindow::setBlockKeyboardEvent(bool blockKeyboardEvent) {
// 	this->blockKeyboardEvent = blockKeyboardEvent;
// }

// bool CSDLWindow::isLockedFocus() const {
// 	return lockedFocus;
// }

// void CSDLWindow::setLockedFocus(bool lockedFocus) {
// 	this->lockedFocus = lockedFocus;
// }

// CBanner * CSDLWindow::showBanner(const std::string & str, int tick) {
// 	CBanner * pBanner = new CBanner(this, str, tick);
// 	return pBanner;
// }

// CDrawableObj * CSDLWindow::getObjectByIdentifier(unsigned int id) {
// 	for (auto item : this->objDrawRespReg)
// 		if (id == item->getIdentifier())
// 			return item;
// 	return NULL;
// }

