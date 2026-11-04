// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
struct GameMainView;
struct HUDCanvasLayout;
struct GameAuxiliarySchedule;
void RunGameCameraCaptureTransitions();
void ValidateGameCameraCapture(const GameMainView&,const HUDCanvasLayout&);
void ValidateGameCameraCapture(const GameAuxiliarySchedule&);
