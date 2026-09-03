#include "ScreenFade.h"
#include "Lerp.h"
#include "imgui.h"

void ScreenFade::StartCovered()
{
	m_alpha     = 1.0f;
	m_target    = 0.0f;
	m_busy      = false;
	m_didAction = false;
	m_onBlack   = nullptr;
}

void ScreenFade::Transition(std::function<void()> onBlack)
{
	if (m_busy) return;			// one transition at a time
	m_busy      = true;
	m_didAction = false;
	m_onBlack   = std::move(onBlack);
	m_target    = 1.0f;			// first: fade out to black
}

void ScreenFade::Update(float dt)
{
	float step = (m_halfTime > 1e-4f) ? dt / m_halfTime : 1.0f;
	m_alpha = Lerp::MoveTowards(m_alpha, m_target, step);

	if (!m_busy) return;

	if (!m_didAction && m_alpha >= 0.999f)		// reached full black
	{
		m_didAction = true;
		if (m_onBlack) m_onBlack();				// switch state hidden by black
		m_target = 0.0f;						// then fade back in
	}
	else if (m_didAction && m_alpha <= 0.001f)	// finished fading in
	{
		m_busy    = false;
		m_onBlack = nullptr;
	}
}

void ScreenFade::Draw() const
{
	if (m_alpha <= 0.001f) return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 sz = ImGui::GetIO().DisplaySize;
	ImU32 col = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, m_alpha));
	dl->AddRectFilled(ImVec2(0, 0), sz, col);
}
