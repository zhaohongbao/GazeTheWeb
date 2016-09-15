//============================================================================
// Distributed under the Apache License, Version 2.0.
// Author: Daniel Mueller (muellerd@uni-koblenz.de)
// Author: Raphael Menges (raphaelmenges@uni-koblenz.de)
//============================================================================

#include "src/State/Web/Tab/Tab.h"
#include "src/State/Web/Tab/Pipelines/ZoomClickPipeline.h"
#include "src/CEF/Extension/CefMediator.h"

void Tab::TabButtonListener::down(eyegui::Layout* pLayout, std::string id)
{
	if (pLayout == _pTab->_pPanelLayout)
	{
		// ### Tab layout ###
		if (id == "click_mode")
		{
			_pTab->PushBackPipeline(std::move(std::unique_ptr<ZoomClickPipeline>(new ZoomClickPipeline(_pTab))));
		}
		else if (id == "auto_scrolling")
		{
			_pTab->_autoScrolling = true;
		}
		else if (id == "scroll_to_top")
		{
			_pTab->_pCefMediator->ResetScrolling(_pTab);
		}
		else if (id == "zoom")
		{
			_pTab->_zoomLevel = 1.3;

			// Trigger zooming in CefMediator
			_pTab->_pCefMediator->SetZoomLevel(_pTab);
		}
	}
	else
	{
		// ### Pipeline abort layout ###
		if (id == "abort")
		{
			_pTab->AbortAndClearPipelines();
		}
	}
}

void Tab::TabButtonListener::up(eyegui::Layout* pLayout, std::string id)
{
	if (pLayout == _pTab->_pPanelLayout)
	{
		// ### Tab layout ###
		if (id == "auto_scrolling")
		{
			_pTab->_autoScrolling = false;
		}
		else if (id == "zoom")
		{
			_pTab->_zoomLevel = 1.0;

			// Trigger zooming in CefMediator
			_pTab->_pCefMediator->SetZoomLevel(_pTab);
		}
	}
	else
	{
		// ### Pipeline abort layout ###
	}
}

void Tab::TabSensorListener::penetrated(eyegui::Layout* pLayout, std::string id, float amount)
{
	if (pLayout == _pTab->_pScrollingOverlayLayout)
	{
		if (id == "scroll_up_sensor")
		{
            _pTab->_pCefMediator->EmulateMouseWheelScrolling(_pTab, 0, amount * 20.f);
		}
		else if (id == "scroll_down_sensor")
		{
            _pTab->_pCefMediator->EmulateMouseWheelScrolling(_pTab, 0, amount * -20.f);
		}
	}
}

void Tab::TabOverlayButtonListener::down(eyegui::Layout* pLayout, std::string id)
{
	// Search for id in map
	auto iter = _pTab->_overlayButtonDownCallbacks.find(id);
	if (iter != _pTab->_overlayButtonDownCallbacks.end())
	{
		// Execute callback
		iter->second();
	}
}

void Tab::TabOverlayButtonListener::up(eyegui::Layout* pLayout, std::string id)
{
	// Search for id in map
	auto iter = _pTab->_overlayButtonUpCallbacks.find(id);
	if (iter != _pTab->_overlayButtonUpCallbacks.end())
	{
		// Execute callback
		iter->second();
	}
}

void Tab::TabOverlayKeyboardListener::keyPressed(eyegui::Layout* pLayout, std::string id, std::u16string value)
{
	// Search for id in map
	auto iter = _pTab->_overlayKeyboardCallbacks.find(id);
	if (iter != _pTab->_overlayKeyboardCallbacks.end())
	{
		// Execute callback
		iter->second(value);
	}
}

void Tab::TabOverlayWordSuggestListener::chosen(eyegui::Layout* pLayout, std::string id, std::u16string value)
{
	// Search for id in map
	auto iter = _pTab->_overlayWordSuggestCallbacks.find(id);
	if (iter != _pTab->_overlayWordSuggestCallbacks.end())
	{
		// Execute callback
		iter->second(value);
	}
}