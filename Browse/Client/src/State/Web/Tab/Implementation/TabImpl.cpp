//============================================================================
// Distributed under the Apache License, Version 2.0.
// Author: Daniel Mueller (muellerd@uni-koblenz.de)
// Author: Raphael Menges (raphaelmenges@uni-koblenz.de)
//============================================================================

#include "src/State/Web/Tab/Tab.h"
#include "src/Master/Master.h"
#include "src/Setup.h"
#include "src/Utils/Helper.h"
#include "src/Utils/Logger.h"
#include "src/State/Web/Tab/SocialRecord.h"
#include <algorithm>

Tab::Tab(
	Master* pMaster,
	Mediator* pCefMediator,
	WebTabInterface* pWeb,
	std::string url,
	bool dataTransfer) : _dataTransfer(dataTransfer)
{
	// Fill members
	_pMaster = pMaster;
	_pCefMediator = pCefMediator;
	_pWeb = pWeb;
	_url = url;

	// Create layouts for Tab (overlay at first, because behind other layouts)
	_pOverlayLayout = _pMaster->AddLayout("layouts/Overlay.xeyegui", EYEGUI_TAB_LAYER, false);
	_pScrollingOverlayLayout = _pMaster->AddLayout("layouts/Overlay.xeyegui", EYEGUI_TAB_LAYER, false);
	_pPanelLayout = _pMaster->AddLayout("layouts/Tab.xeyegui", EYEGUI_TAB_LAYER, false);
	_pPipelineAbortLayout = _pMaster->AddLayout("layouts/TabPipelineAbort.xeyegui", EYEGUI_TAB_LAYER, false);
    _pDebugLayout = _pMaster->AddLayout("layouts/TabDebug.xeyegui", EYEGUI_TAB_LAYER, false);

	// Create scroll up and down floating frames in special overlay layout which always on top of standard overlay
	_scrollUpProgressFrameIndex = eyegui::addFloatingFrameWithBrick(
		_pScrollingOverlayLayout,
		"bricks/TabScrollUpProgress.beyegui",
		0.5f - (TAB_SCROLLING_SENSOR_WIDTH / 2.f),
		TAB_SCROLLING_SENSOR_PADDING,
		TAB_SCROLLING_SENSOR_WIDTH,
		TAB_SCROLLING_SENSOR_HEIGHT,
		false,
		false);
	_scrollDownProgressFrameIndex = eyegui::addFloatingFrameWithBrick(
		_pScrollingOverlayLayout,
		"bricks/TabScrollDownProgress.beyegui",
		0.5f - (TAB_SCROLLING_SENSOR_WIDTH / 2.f),
		1.f - TAB_SCROLLING_SENSOR_PADDING - TAB_SCROLLING_SENSOR_HEIGHT,
		TAB_SCROLLING_SENSOR_WIDTH,
		TAB_SCROLLING_SENSOR_HEIGHT,
		false,
		false);
	_scrollUpSensorFrameIndex = eyegui::addFloatingFrameWithBrick(
		_pScrollingOverlayLayout,
		"bricks/TabScrollUpSensor.beyegui",
		0.5f - (TAB_SCROLLING_SENSOR_WIDTH / 2.f),
		TAB_SCROLLING_SENSOR_PADDING,
		TAB_SCROLLING_SENSOR_WIDTH,
		TAB_SCROLLING_SENSOR_HEIGHT,
		false,
		false);
	_scrollDownSensorFrameIndex = eyegui::addFloatingFrameWithBrick(
		_pScrollingOverlayLayout,
		"bricks/TabScrollDownSensor.beyegui",
		0.5f - (TAB_SCROLLING_SENSOR_WIDTH / 2.f),
		1.f - TAB_SCROLLING_SENSOR_PADDING - TAB_SCROLLING_SENSOR_HEIGHT,
		TAB_SCROLLING_SENSOR_WIDTH,
		TAB_SCROLLING_SENSOR_HEIGHT,
		false,
		false);

	// Register listener
	_spTabButtonListener = std::shared_ptr<TabButtonListener>(new TabButtonListener(this));
	_spTabSensorListener = std::shared_ptr<TabSensorListener>(new TabSensorListener(this));
	eyegui::registerButtonListener(_pPanelLayout, "click_mode", _spTabButtonListener);
	eyegui::registerButtonListener(_pPanelLayout, "auto_scrolling", _spTabButtonListener);
    eyegui::registerButtonListener(_pPanelLayout, "scroll_to_top", _spTabButtonListener);
    // eyegui::registerButtonListener(_pPanelLayout, "pivot", _spTabButtonListener);
    // eyegui::registerButtonListener(_pPanelLayout, "gaze_mouse", _spTabButtonListener);
    eyegui::registerButtonListener(_pPanelLayout, "selection", _spTabButtonListener);
	eyegui::registerButtonListener(_pPanelLayout, "zoom", _spTabButtonListener);
	// eyegui::registerButtonListener(_pPanelLayout, "test_button", _spTabButtonListener); // TODO: only for testing new features
	eyegui::registerButtonListener(_pPipelineAbortLayout, "abort", _spTabButtonListener);
	eyegui::registerSensorListener(_pScrollingOverlayLayout, "scroll_up_sensor", _spTabSensorListener);
	eyegui::registerSensorListener(_pScrollingOverlayLayout, "scroll_down_sensor", _spTabSensorListener);

	// Create listener for overlay
	_spTabOverlayButtonListener = std::shared_ptr<TabOverlayButtonListener>(new TabOverlayButtonListener(this));
	_spTabOverlayKeyboardListener = std::shared_ptr<TabOverlayKeyboardListener>(new TabOverlayKeyboardListener(this));
	_spTabOverlayWordSuggestListener = std::shared_ptr<TabOverlayWordSuggestListener>(new TabOverlayWordSuggestListener(this));

	// Create WebView owned by Tab
	auto webViewInGUI = eyegui::getAbsolutePositionAndSizeOfElement(_pPanelLayout, "web_view");
    _upWebView = std::unique_ptr<WebView>(new WebView(webViewInGUI.x, webViewInGUI.y, webViewInGUI.width, webViewInGUI.height));

	// Register itself and painted texture in mediator to receive DOMNodes
	_pCefMediator->RegisterTab(this);

	// Prepare debugging overlay
	InitDebuggingOverlay();
}

Tab::~Tab()
{
	// Store current social record if available
	EndSocialRecord();

	// Finish persisting social records before destruction
	UpdatePersisting(true); // do wait so block

	// Delete DOM Nodes and triggers (right now only DOMTriggers) before removing layout
	ClearDOMNodes();

	// Abort pipeline
	AbortAndClearPipelines();

	_pCefMediator->UnregisterTab(this);
	_pMaster->RemoveLayout(_pPanelLayout);
	_pMaster->RemoveLayout(_pPipelineAbortLayout);
	_pMaster->RemoveLayout(_pOverlayLayout);
	_pMaster->RemoveLayout(_pScrollingOverlayLayout);
    _pMaster->RemoveLayout(_pDebugLayout);
}

void Tab::Update(float tpf, const std::shared_ptr<const Input> spInput)
{
	// Store tpf
	_lastTimePerFrame = tpf;

	// #######################
	// ### UPDATE WEB VIEW ###
	// #######################

	// Update WebView (get values from eyeGUI layout directly)
	const auto webViewInGUI = eyegui::getAbsolutePositionAndSizeOfElement(_pPanelLayout, "web_view");
	_upWebView->Update(
		webViewInGUI.x,
		webViewInGUI.y,
		webViewInGUI.width,
		webViewInGUI.height);

	// ######################
	// ### UPDATE OVERLAY ###
	// ######################

	// Update click visualization
	for (int i = 0; i < (int)_clickVisualizations.size(); i++)
	{
		// Decrement visibility
		_clickVisualizations.at(i).fading -= tpf;

		// If fading done, remove from layout
		if (_clickVisualizations.at(i).fading <= 0)
		{
			eyegui::removeFloatingFrame(_pOverlayLayout, _clickVisualizations.at(i).frameIndex, false);
		}
		else
		{
			// Fade it by making it smaller
			float relativeSize = (_clickVisualizations.at(i).fading / CLICK_VISUALIZATION_DURATION) * CLICK_VISUALIZATION_RELATIVE_SIZE;

			// Position of floating frame
			float relativePositionX = (_upWebView->GetX() + _clickVisualizations.at(i).x) / (float)_pMaster->GetWindowWidth();
			float relativePositionY = (_upWebView->GetY() + _clickVisualizations.at(i).y) / (float)_pMaster->GetWindowHeight();
			relativePositionX -= relativeSize / 2.f;
			relativePositionY -= relativeSize / 2.f;

			// Tell eyeGUI about position and size of floating frame
			eyegui::setPositionOfFloatingFrame(_pOverlayLayout, _clickVisualizations.at(i).frameIndex, relativePositionX, relativePositionY);
			eyegui::setSizeOfFloatingFrame(_pOverlayLayout, _clickVisualizations.at(i).frameIndex, relativeSize, relativeSize);
		}
	}

	// Erase visualizations which are no more in layout
	_clickVisualizations.erase(
		std::remove_if(
			_clickVisualizations.begin(),
			_clickVisualizations.end(),
            [](const ClickVisualization& i) { return i.fading <= 0; }),
		_clickVisualizations.end());

	// ########################
	// ### TAB INPUT STRUCT ###
	// ########################

	// Create tab input structure (like standard input but in addition with input coordinates in web view space)
	const std::shared_ptr<TabInput> spTabInput = std::make_shared<TabInput>(
		spInput,
		_upWebView->GetX(),
		_upWebView->GetY(),
		_upWebView->GetWidth(),
		_upWebView->GetHeight(),
		_upWebView->GetResolutionX(),
		_upWebView->GetResolutionY()
		);

	// Update highlight rectangle of webview
	// TODO: alternative: give webview shared pointer to DOM nodes
	std::vector<Rect> rects;
	for (const auto& rIdNodePair : _TextLinkMap)
	{
		if (!rIdNodePair.second)
			continue;

		for (const auto& rRect : rIdNodePair.second->GetRects())
		{
			// TODO: Sometimes breaks at this position in debug mode
			rects.push_back(rRect);
		}
	}
	_upWebView->SetHighlightRects(rects);

	// ###########################
	// ### UPDATE COLOR OF GUI ###
	// ###########################

	UpdateAccentColor(tpf);

	// ###################
	// ### UPDATE ICON ###
	// ###################

    if (_iconState == IconState::ICON_NOT_FOUND && _faviconLoaded)
	{
        // Favicon has been loaded now
        eyegui::setImageOfPicture(_pPanelLayout, "icon", GetFaviconIdentifier());
        _iconState = IconState::FAVICON;
	}
    else if (_iconState == IconState::LOADING)
	{
		// Update frame of loading icon
		_timeUntilNextLoadingIconFrame -= tpf;
		while (_timeUntilNextLoadingIconFrame < 0)
		{
			_timeUntilNextLoadingIconFrame += TAB_LOADING_ICON_FRAME_DURATION;
			_loadingIconFrame = (_loadingIconFrame + 1) % 3;
		}
		eyegui::setImageOfPicture(_pPanelLayout, "icon", "icons/TabLoading_" + std::to_string(_loadingIconFrame) + ".png");
	}

	// ###########################
    // ### UPDATE DEBUG LAYOUT ###
	// ###########################

	// Push back current gaze for debugging purposes
	_gazeDebuggingQueue.push_front(glm::vec2(spInput->gazeX, spInput->gazeY));

	// Limit length of queue
	int toPop = (int)_gazeDebuggingQueue.size() - TAB_DEBUGGING_GAZE_COUNT;
	for (int i = 0; i < toPop; i++)
	{
		_gazeDebuggingQueue.pop_back();
	}

	// Update text in layout
    eyegui::setContentOfTextBlock(
        _pDebugLayout,
        "web_view_coordinate",
        "Fixed:\n"
        + std::to_string(spTabInput->CEFPixelGazeX) + ", " + std::to_string(spTabInput->CEFPixelGazeY) + "\n"
        + "Scrolled:\n"
        + std::to_string((int)(spTabInput->CEFPixelGazeX + _scrollingOffsetX)) + ", " + std::to_string((int)(spTabInput->CEFPixelGazeY + _scrollingOffsetY)));

	// #######################################
    // ### UPDATE PIPELINE OR STANDARD GUI ###
	// #######################################

	// Decide what to update
	if (_pipelineActive)
	{
		// PIPELINE GETS UPDATED. STANDARD GUI NOT VISIBLE

		// Disable input for panel layout and enable it for pipeline abort layout
		eyegui::setInputUsageOfLayout(_pPanelLayout, false);
		eyegui::setInputUsageOfLayout(_pPipelineAbortLayout, true);

		// Update current pipeline (if there is one)
		if (_pipelines.front()->Update(tpf, spTabInput))
		{
			// Remove front element from pipelines
			_pipelines.front()->Deactivate();
			_pipelines.pop_front();

			// Activate new pipeline if there is some
			if (!(_pipelines.empty()))
			{
				_pipelines.front()->Activate();
			}
			else
			{
				// Remember that there is no active pipeline and deactivate GUI to abort it
				SetPipelineActivity(false);
			}
		}
	}
	else
	{
        // STANDARD GUI IS VISIBLE

        // Gaze mouse
        if(_gazeMouse && !(_pMaster->IsPaused()))
        {
            EmulateMouseCursor(spTabInput->webViewPixelGazeX, spTabInput->webViewPixelGazeY);
        }

		// Ask for page resolution
		_timeUntilGetPageResolution -= tpf;
		if (_timeUntilGetPageResolution <= 0)
		{
			_timeUntilGetPageResolution = TAB_GET_PAGE_RES_INTERVAL;
			_pCefMediator->GetPageResolution(this);
		}

		// Enable input for panel layout and disable it for pipeline abort layout
		eyegui::setInputUsageOfLayout(_pPanelLayout, true);
		eyegui::setInputUsageOfLayout(_pPipelineAbortLayout, false);

		// Manual scrolling
		bool showScrollUp = false;
		bool showScrollDown = false;
		if (!_autoScrolling)
		{
			// Scroll up
			if (_scrollingOffsetY > 0)
			{
				showScrollUp = true;
			}

			// Scroll down
			if ((_pageHeight - 1) > (_scrollingOffsetY + _upWebView->GetResolutionY()))
			{
				showScrollDown = true;
			}
		}

		// Set progress of scrolling
		float scrollableHeight = (_pageHeight - 1) - _upWebView->GetResolutionY();
		float progressUp = 1.f;
		float progressDown = 1.f;
		if (scrollableHeight > 0)
		{
            progressUp = _scrollingOffsetY / scrollableHeight;
            progressDown = 1.f - progressUp;
		}
		eyegui::setProgress(_pScrollingOverlayLayout, "scroll_up_progress", progressUp);
		eyegui::setProgress(_pScrollingOverlayLayout, "scroll_down_progress", progressDown);

		// Set visibility of scroll elements
		eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollUpProgressFrameIndex, showScrollUp, false, true);
		eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollDownProgressFrameIndex, showScrollDown, false, true);
		eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollUpSensorFrameIndex, showScrollUp, false, true);
		eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollDownSensorFrameIndex, showScrollDown, false, true);

		// Set activity of scroll to top button
		eyegui::setElementActivity(_pPanelLayout, "scroll_to_top", showScrollUp, true);

		// Check, that gaze is not upon a fixed element
		bool gazeUponFixed = false;
		for (const auto& rElements : _fixedElements)
		{
			for (const auto& rElement : rElements)
			{
				// Simple box test
				if(rElement.IsInside(spTabInput->CEFPixelGazeX, spTabInput->CEFPixelGazeY))
				{
					gazeUponFixed = true;
					break;
				}
			}
			if (gazeUponFixed) { break; }
		}

		// Automatic scrolling. Check whether gaze is available inside webview
		if (_autoScrolling && !spTabInput->gazeUponGUI && spTabInput->insideWebView && !gazeUponFixed)
		{
			// Only vertical scrolling supported, yet (TODO: parameters)
			float yOffset = -spTabInput->webViewRelativeGazeY + 0.5f;
			yOffset *= 2; // [-1..1]
			bool negative = yOffset < 0;
			yOffset = yOffset * yOffset; // [0..1]
			// yOffset = (glm::max(0.f, yOffset - 0.05f) / 0.95f); // In the center of view no movement
			yOffset = negative ? -yOffset : yOffset; // [-1..1]

			// Update the auto scrolling value (TODO: not really frame rate independend)
			_autoScrollingValue += tpf * (yOffset - _autoScrollingValue);
		}
		else if (_autoScrollingValue != 0)
		{
			// Fade it out since auto scrolling still ongoing
			if (_autoScrollingValue > 0)
			{
				_autoScrollingValue -= tpf;
				_autoScrollingValue = glm::max(0.f, _autoScrollingValue);
			}
			else
			{
				_autoScrollingValue += tpf;
				_autoScrollingValue = glm::min(0.f, _autoScrollingValue);
			}
		}

		// Use value of auto scrolling to scroll
        if(_autoScrollingValue != 0.0f)
        {
            _pCefMediator->EmulateMouseWheelScrolling(this, 0.0, (double)(20.f * _autoScrollingValue));
        }

		// Autoscroll inside of DOMOverflowElement if gazed upon
		for (auto& rIdOverflowPair : _OverflowElementMap)
		{
			auto& rspOverflowElement = rIdOverflowPair.second;
			if (rspOverflowElement)
			{
				for (const auto& rRect : rspOverflowElement->GetRects())
				{
					int scrolledCEFPixelGazeX = spTabInput->CEFPixelGazeX;
					int scrolledCEFPixelGazeY = spTabInput->CEFPixelGazeY;

					// Add scrolling offset if element is not fixed
					if (!(rspOverflowElement->IsFixed()))
					{
						scrolledCEFPixelGazeX += _scrollingOffsetX;
						scrolledCEFPixelGazeY += _scrollingOffsetY;
					}

					// Check if current gaze is inside of overflow element, if so execute scrolling method in corresponding Javascript object
					if (rRect.IsInside(scrolledCEFPixelGazeX, scrolledCEFPixelGazeY))
					{
						rspOverflowElement->Scroll(scrolledCEFPixelGazeX, scrolledCEFPixelGazeY);
						//rspOverflowElement->Scroll(spTabInput->CEFPixelGazeX, spTabInput->CEFPixelGazeY);
						break;
					}
				}
			}
		}

		// #######################
		// ### UPDATE TRIGGERS ###
		// #######################

		for (auto pTrigger : _triggers)
		{
			pTrigger->Update(tpf, spTabInput);
		}

		// ############################
		// ### UPDATE SOCIAL RECORD ###
		// ############################

		// Update persisting
		UpdatePersisting(false); // do not wait so do not block
		
		// Check for URL change and whether to start new social record
		if (_url != BLANK_PAGE_URL && _prevURL != _url)
		{
			// Classify URL, which platform was entered?
			auto platform = SocialRecord::ClassifyURL(_url);

			// Decide how to proceed after URL change
			if (_spSocialRecord == nullptr) // no current record going on
			{
				StartSocialRecord(_url, platform);
			}
			else if (_spSocialRecord->GetPlatform() != platform) // record for different platform going on
			{
				// Store current record
				EndSocialRecord();

				// Start new record
				StartSocialRecord(_url, platform);
			}
			else if ((_spSocialRecord->GetPlatform() == SocialPlatform::Unknown) && (_spSocialRecord->GetDomain() != SocialRecord::ExtractDomain(_url))) // record for unknown platform going on but domain changes 
			{
				// Store current record
				EndSocialRecord();

				// Start new record
				StartSocialRecord(_url, platform);
			}
			else // recording of platform continues
			{
				_spSocialRecord->AddPage(_url); // seems that a subpage was opened
			}

			// Update previous URL
			_prevURL = _url; // store new URL
		}
		else // no URL change, just update ongoing social record
		{
			// Update ongoing social record
			if (_spSocialRecord != nullptr)
			{
				// Check for active user
				bool userActive = spInput->gazeEmulated || (spInput->gazeAge < setup::MAX_AGE_OF_USED_GAZE); // taken partly from Master.cpp

				// Update social record
				_spSocialRecord->AddTimeInForeground(tpf); // this update is only called when in foreground
				if (userActive) { _spSocialRecord->AddTimeActiveUser(tpf); }// add on duration while user is active
				_spSocialRecord->AddScrollingDelta(glm::abs(_scrollingOffsetY - _prevScrolling));

				// Prepare next update
				_prevScrolling = _scrollingOffsetY; // scrolling delta helper
			}
		}
	}
}

void Tab::Draw() const
{
	// Draw WebView
	_upWebView->Draw(
		_webViewParameters,
		_pMaster->GetWindowWidth(),
		_pMaster->GetWindowHeight(),
		_scrollingOffsetX,
		_scrollingOffsetY);

	// Decide what to draw
	if (_pipelineActive)
	{
		// Draw stuff from current pipeline (overlay etc.)
		_pipelines.front()->Draw();
	}
	else
	{
		// Draw triggers
		for (auto pTrigger : _triggers)
		{
			pTrigger->Draw();
		}
	}

	// Draw debug overlay
#ifdef CLIENT_DEBUG
		DrawDebuggingOverlay();
#endif
}

void Tab::Activate()
{
	// Show layouts
	eyegui::setVisibilityOfLayout(_pOverlayLayout, true, true, false);
	eyegui::setVisibilityOfLayout(_pScrollingOverlayLayout, true, true, false);
	eyegui::setVisibilityOfLayout(_pPanelLayout, true, true, false);
    eyegui::setVisibilityOfLayout(_pDebugLayout, setup::DEBUG_MODE, true, false);

	// Setup switches
	if (_autoScrolling) { eyegui::buttonDown(_pPanelLayout, "auto_scrolling", true); }
	if (_zoomLevel != 1.0) { eyegui::buttonDown(_pPanelLayout, "zoom", true); }

	// Screw color interpolation
	_currentColorAccent = _targetColorAccent;
	_colorInterpolation = 1.f;

	// Remember being active
	_active = true;
}

void Tab::Deactivate()
{
	// Hide layouts
	eyegui::setVisibilityOfLayout(_pOverlayLayout, false, true, false);
	eyegui::setVisibilityOfLayout(_pScrollingOverlayLayout, false, true, false);
	eyegui::setVisibilityOfLayout(_pPanelLayout, false, true, false);
    eyegui::setVisibilityOfLayout(_pDebugLayout, false, true, false);

	// TODO: THIS SHOULD NOT BE NECESSARY SINCE _pScrollingOverlayLayout IS HIDDEN! WHY?
	eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollUpProgressFrameIndex, false, false, true);
	eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollDownProgressFrameIndex, false, false, true);
	eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollUpSensorFrameIndex, false, false, true);
	eyegui::setVisibilityOFloatingFrame(_pScrollingOverlayLayout, _scrollDownSensorFrameIndex, false, false, true);

	// Abort pipeline, which also hides GUI for manual abortion
	AbortAndClearPipelines();

	// Remember being not active
	_active = false;
}

void Tab::OpenURL(std::string URL)
{
	// Set URL
	_url = URL;

	// Abort any pipeline execution
	AbortAndClearPipelines();

	// Tell CEF to refresh this tab (which will fetch the URL and load it)
	_pCefMediator->RefreshTab(this);

	// Reset scrolling
	_scrollingOffsetX = 0.0;
	_scrollingOffsetY = 0.0;
	_prevScrolling = 0.0;
}

void Tab::AbortAndClearPipelines()
{
	// Clear pipeline
	_pipelines.clear(); // Destructor of pipeline aborts them
	SetPipelineActivity(false);
}

void Tab::GoForward()
{
	_pCefMediator->GoForward(this);

	// Abort any pipeline execution
	AbortAndClearPipelines();
}

void Tab::GoBack()
{
	_pCefMediator->GoBack(this);

	// Abort any pipeline execution
	AbortAndClearPipelines();
}

void Tab::Reload()
{
	_pCefMediator->ReloadTab(this);

	// Abort any pipeline execution
	AbortAndClearPipelines();
}

void Tab::PushBackPointingEvaluationPipeline(PointingApproach approach)
{
	PushBackPipeline(std::unique_ptr<PointingEvaluationPipeline>(new PointingEvaluationPipeline(this, approach)));
}

void Tab::SetDataTransfer(bool active)
{
	_dataTransfer = active;
	if (_dataTransfer == false)
	{
		// Abort current social record
		_spSocialRecord = nullptr;
	}
}

void Tab::NotifyClick(std::string tag, std::string id)
{
	if (_spSocialRecord != nullptr)
	{
		_spSocialRecord->AddClick(tag, id);
	}
}

void Tab::SetPipelineActivity(bool active)
{
	if (active)
	{
		_pipelineActive = true;
		eyegui::setVisibilityOfLayout(_pPipelineAbortLayout, true, true, true);

		// Deactivate all triggers
		for (auto pTrigger : _triggers)
		{
			pTrigger->Deactivate();
		}

		// Deactivate scrolling overlay
		eyegui::setVisibilityOfLayout(_pScrollingOverlayLayout, false, false, true);
	}
	else
	{
		_pipelineActive = false;
		eyegui::setVisibilityOfLayout(_pPipelineAbortLayout, false, false, true);

		// Activate all triggers
		for (auto pTrigger : _triggers)
		{
			pTrigger->Activate();
		}

		// Activate scrolling overlay
		eyegui::setVisibilityOfLayout(_pScrollingOverlayLayout, true, true, true);
	}
}

void Tab::StartSocialRecord(std::string URL, SocialPlatform platform)
{
	if (_dataTransfer) // only proceed when data transfer is allowed
	{
		// End current social record if necessary
		if (_spSocialRecord != nullptr)
		{
			EndSocialRecord(); // makes social record null
		}

		// Start new record
		_spSocialRecord = std::shared_ptr<SocialRecord>(new SocialRecord(SocialRecord::ExtractDomain(URL), platform));
		_spSocialRecord->StartAndAddPage(URL);
	}
}

void Tab::EndSocialRecord()
{
	if (_spSocialRecord != nullptr)
	{
		// Tell record to end
		_spSocialRecord->End();

		// Delegate persisting into thread
		_asyncPersisting.push_back(std::async(
			std::launch::async, // do it asynchronously
			[](std::shared_ptr<SocialRecord> spSocialRecord) // provide copy of shared pointer
		{
			// Store in database
			spSocialRecord->Persist();
			return true; // give the future some value
		}, _spSocialRecord));

		// Make local copy of record null
		_spSocialRecord = nullptr;
	}
}

void Tab::UpdatePersisting(bool wait)
{
	// Check asynchronous persisting
	for (int i = _asyncPersisting.size() - 1; i >= 0; i--) // do it from the back
	{
		// Retrieve whether asynchronous call is done
		bool remove = false;
		if (wait)
		{
			_asyncPersisting.at(i).wait();
			remove = true; // waited until it is done, so remove it
		}
		else
		{
			// We have no time, so wait for nothing and just check
			if (std::future_status::ready == _asyncPersisting.at(i).wait_for(std::chrono::seconds(0)))
			{
				remove = true; // done by now, so remove it
			}
		}

		// Remove future from list when persisting is done
		if (remove)
		{
			_asyncPersisting.erase(_asyncPersisting.begin() + i);
		}
	}
}

void Tab::UpdateAccentColor(float tpf)
{
	// Move accent color accent towards target accent color
	glm::vec4 colorAccent = _currentColorAccent;
	if (_colorInterpolation < 1.f)
	{
		_colorInterpolation += tpf; // interpolate
		_colorInterpolation = glm::min(_colorInterpolation, 1.f); // clamp
		colorAccent = ((1.f - _colorInterpolation) * _currentColorAccent) + (_colorInterpolation * _targetColorAccent); // calculation of current color
		if (_colorInterpolation == 1.f)
		{
			_currentColorAccent = _targetColorAccent;
		}
	}

	// Create background color
	float backgroundMultiplier = 0.5f;
	float panelBackgroundMultiplier = 0.4f;
	glm::vec4 backgroundAccentColor(
		colorAccent.r * backgroundMultiplier,
		colorAccent.g * backgroundMultiplier,
		colorAccent.b * backgroundMultiplier,
		colorAccent.a);
	glm::vec4 panelBackgroundAccentColor(
		colorAccent.r * panelBackgroundMultiplier,
		colorAccent.g * panelBackgroundMultiplier,
		colorAccent.b * panelBackgroundMultiplier,
		colorAccent.a);

	// Create transparent colors
	float alpha = 0.75f;
	float backgroundAlpha = 0.5f;
	glm::vec4 transparentColorAccent = glm::vec4(
		colorAccent.r,
		colorAccent.g,
		colorAccent.b,
		colorAccent.a * alpha);
	glm::vec4 transparentBackgroundColorAccent = glm::vec4(
		backgroundAccentColor.r,
		backgroundAccentColor.g,
		backgroundAccentColor.b,
		backgroundAccentColor.a * backgroundAlpha);

	// Set color of tab_panel style
	_pMaster->SetStyleTreePropertyValue(
		"tab_panel",
		eyegui::property::Color::Color,
		RGBAToHexString(colorAccent)
	);

	// Set color of tab_overlay style
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay",
		eyegui::property::Color::Color,
		RGBAToHexString(colorAccent)
	);
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay",
		eyegui::property::Color::BackgroundColor,
		RGBAToHexString(backgroundAccentColor)
	);

	// Set color of tab_overlay_noback style
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay_noback",
		eyegui::property::Color::Color,
		RGBAToHexString(colorAccent)
	);

	// Set color of tab_overlay_panel style
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay_panel",
		eyegui::property::Color::Color,
		RGBAToHexString(backgroundAccentColor)
	);
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay_panel",
		eyegui::property::Color::BackgroundColor,
		RGBAToHexString(panelBackgroundAccentColor)
	);

	// Set color and background of tab_overlay_scroll_progress
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay_scroll_progress",
		eyegui::property::Color::Color,
		RGBAToHexString(transparentColorAccent)
	);
	_pMaster->SetStyleTreePropertyValue(
		"tab_overlay_scroll_progress",
		eyegui::property::Color::BackgroundColor,
		RGBAToHexString(transparentBackgroundColorAccent)
	);
}

void Tab::PushBackClickVisualization(double x, double y)
{
	// Structure for click visulization
	ClickVisualization clickVisualization;
	clickVisualization.x = x;
	clickVisualization.y = y;
	clickVisualization.fading = CLICK_VISUALIZATION_DURATION;

	// Position of floating frame
	float relativePositionX = (_upWebView->GetX() + x) / (float)_pMaster->GetWindowWidth();
	float relativePositionY = (_upWebView->GetY() + y) / (float)_pMaster->GetWindowHeight();
	relativePositionX -= CLICK_VISUALIZATION_RELATIVE_SIZE / 2.f;
	relativePositionY -= CLICK_VISUALIZATION_RELATIVE_SIZE / 2.f;

	// Add floating frame to overlay
	clickVisualization.frameIndex = eyegui::addFloatingFrameWithBrick(
		_pOverlayLayout,
		"bricks/TabClickVisualization.beyegui",
		relativePositionX,
		relativePositionY,
		CLICK_VISUALIZATION_RELATIVE_SIZE,
		CLICK_VISUALIZATION_RELATIVE_SIZE,
		true,
		false);

	// Add to vector which is updated per frame
	_clickVisualizations.push_back(clickVisualization);
}

std::string Tab::GetFaviconIdentifier() const
{
	return "tab_info_" + std::to_string(_pWeb->GetIdOfTab(this));
}