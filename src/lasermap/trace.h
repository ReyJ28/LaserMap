// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "types.h"

namespace lasermap {

struct TraceParams {
  enum Source { Luma = 0, Alpha = 1, Edges = 2 };
  Source source = Luma;       // which signal is thresholded into shapes
  float threshold = 0.5f;     // iso-level 0..1 that defines the outline
  float simplifyEps = 1.0f;   // Douglas-Peucker tolerance in source pixels
  float minPerimeter = 8.0f;  // drop contours shorter than this (pixels)
  int maxShapes = 64;         // keep only the N largest shapes
  float smoothing = 0.f;      // 0..1, blur applied to the signal before thresholding
  bool colourKey = false;     // only keep pixels near keyHue
  float keyHue = 0.f;         // 0..1 (0 = red)
  float keyTolerance = 0.25f; // 0..1, fraction of half the hue circle
  bool keyWhite = false;      // also keep near-white pixels
  int maxPoints = 4000;       // hard budget for one output frame
  float contourSmooth = 0.5f; // 0..1: removes pixel wobble from curves; real corners stay sharp
  float cornerAngleDeg = 50.f; // a turn sharper than this counts as a corner and is never smoothed
  bool optimiseOrder = true;  // draw shapes in a short path order, each loop starting at its nearest point
};

// The scalar field that is thresholded (source selection, smoothing and colour keying applied).
std::vector<float> buildField(const Frame& f, const TraceParams& p);
// Binary mask (255 where the field is >= threshold), w*h, row 0 = top. Used by "Show Mask".
std::vector<uint8_t> maskImage(const Frame& f, const TraceParams& p);
// Marching squares on an arbitrary w*h field.
std::vector<std::vector<Vec2>> findContoursField(const std::vector<float>& field, int w, int h, float threshold);

// Per-pixel luminance 0..1 (Rec.709), alpha-weighted.
std::vector<float> luminance(const Frame& f);

// Closed iso-contours (marching squares) of luminance >= threshold.
// Coordinates are in pixel space (x right, y down).
std::vector<std::vector<Vec2>> findContours(const Frame& f, float threshold);

// Douglas-Peucker on a closed polygon.
std::vector<Vec2> simplifyClosed(const std::vector<Vec2>& poly, float eps);

// A closed outline in laser coordinates ([-1,1], y up) with per-point colour. The last point
// connects back to the first. All beam modes are built from these.
struct Shape {
  std::vector<Point> pts;
};

// Total length of the blanked jumps needed to draw the shapes in this order (each loop closes on its start point).
float blankTravel(const std::vector<Shape>& shapes, Vec2 start = {-1.f, 1.f});
// Greedy nearest-neighbour order: repeatedly pick the shape with a vertex closest to the pen and rotate that loop so
// it starts there. Shortens blanked travel (less flicker, more light on the lines) and keeps the order stable.
void orderShapes(std::vector<Shape>& shapes, Vec2 start = {-1.f, 1.f});

// Find, simplify and colour the outlines of the frame (budgeted by p.maxPoints).
std::vector<Shape> extractShapes(const Frame& f, const TraceParams& p = {});

// Emit shapes as blanked-jump + lit loop; spacing > 0 resamples each edge to that point spacing.
PointFrame emitOutline(const std::vector<Shape>& shapes, float spacing = 0.f);

// "Trace Clip" mode: outline the bright shapes of the frame as laser points.
// Output is a blank jump to each contour start followed by the lit loop.
PointFrame traceClip(const Frame& f, const TraceParams& p = {});

}  // namespace lasermap
