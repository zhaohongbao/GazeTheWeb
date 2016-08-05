//============================================================================
// Distributed under the Apache License, Version 2.0.
// Author: Raphael Menges (raphaelmenges@uni-koblenz.de)
//============================================================================
// Action to zoom to a coordinate with gaze.

// Notes
// - Input: none
// - Output: vec2 coordinate

#ifndef ZOOMCOORDINATEACTION_H_
#define ZOOMCOORDINATEACTION_H_

#include "Action.h"
#include "src/Utils/LerpValue.h"

class ZoomCoordinateAction : public Action
{
public:

    // Constructor
    ZoomCoordinateAction(TabInteractionInterface* pTab);

    // Update retuns whether finished with execution
    virtual bool Update(float tpf, TabInput tabInput);

    // Draw
    virtual void Draw() const;

    // Activate
    virtual void Activate();

    // Deactivate
    virtual void Deactivate();

    // Abort
    virtual void Abort();

protected:

	// Dimming duration
	const float _dimmingDuration = 0.5f; // seconds until it is dimmed

	// Dimming value
	const float _dimmingValue = 0.25f;

    // Coordinate which is updated and output
    glm::vec2 _coordinate;

    // Log zooming amount (used for rendering)
    float _logZoom = 1.f;

    // Linear zooming amout (used for calculations)
    float _linZoom = 1.f;

    // Offset to center of webview
    glm::vec2 _coordinateCenterOffset;

    // Bool to indicate first update
    bool _firstUpdate = true;

	// Deviation (relative coordiantes, no pixels!)
	float _deviation = 0;

	// Dimming
	float _dimming = 0.f;
};

#endif // ZOOMCOORDINATEACTION_H_
