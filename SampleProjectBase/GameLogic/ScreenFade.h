#ifndef __SCREEN_FADE_H__
#define __SCREEN_FADE_H__

#include <functional>

// ============================================================================
//  ScreenFade : a reusable full-screen fade-to-black for scene / state changes.
//
//  It drives one opacity value in [0,1] (0 = clear, 1 = opaque black) and draws
//  a black rectangle over the whole display using ImGui's foreground draw list,
//  so it is resolution independent and always on top of the HUD.
//
//  Typical use is a one-shot Transition(): the screen fades OUT to black, the
//  supplied callback runs once at full black (that is where the real state
//  switch happens, hidden by the black), then it fades back IN. Query IsBusy()
//  to freeze input while a transition is mid-flight.
//
//  The opacity is moved with Lerp::MoveTowards so it reaches its target exactly
//  (no asymptotic tail), giving a predictable, fixed-length fade.
// ============================================================================
class ScreenFade
{
public:
	// Begin fully black and fade in to clear. Call once at app / scene start.
	void StartCovered();

	// One-shot: fade out to black, run onBlack once at full black, then fade in.
	// Ignored while another transition is still running.
	void Transition(std::function<void()> onBlack);

	void  Update(float dt);		// advance the fade; call every frame
	void  Draw() const;			// draw the black overlay (no-op if fully clear)
	bool  IsBusy()  const { return m_busy; }	// true during a Transition
	float Alpha()   const { return m_alpha; }

	float m_halfTime = 0.35f;	// seconds for one half (fade-out or fade-in)

private:
	float m_alpha     = 1.0f;	// current opacity (starts covered)
	float m_target    = 0.0f;
	bool  m_busy      = false;	// inside a Transition (out -> black -> in)
	bool  m_didAction = false;	// onBlack already fired this transition
	std::function<void()> m_onBlack;
};

#endif // __SCREEN_FADE_H__
