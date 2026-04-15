#include "imgui_custom.hpp"
#ifdef IMGUI_DISABLE
#undef IMGUI_DISABLE
#endif

#include <map>
#include <vector>
#include <string>

bool ImGui::HorizontalColorPicker(ImColor* col, ImVec2 size, bool border)
{
	ImColor colors[] = {
		ImColor(255, 0, 0),
		ImColor(255, 255, 0),
		ImColor(0, 255, 0),
		ImColor(0, 255, 255),
		ImColor(0, 0, 255),
		ImColor(255, 0, 255),
		ImColor(255, 0, 0)
	};

	ImVec2 picker_pos = ImGui::GetCursorScreenPos();

	if (border) {
		ImGui::GetCurrentWindow()->DrawList->AddRectFilled(
			ImVec2(picker_pos.x - 2, picker_pos.y - 2),
			ImVec2(picker_pos.x + size.x + 2,
				picker_pos.y + size.y + 2),
			ImGui::GetColorU32(ImGuiCol_Border)
		);
	}

	for (int i = 0; i < 6; ++i)
	{
		ImGui::GetCurrentWindow()->DrawList->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + i * (size.x / 6),
				picker_pos.y),
			ImVec2(picker_pos.x + (i + 1) * (size.x / 6),
				picker_pos.y + size.y),
			colors[i],
			colors[i + 1],
			colors[i + 1],
			colors[i]
		);
	}

	float hue, saturation, value;
	ImGui::ColorConvertRGBtoHSV(
		col->Value.x, col->Value.y, col->Value.z, hue, saturation, value);
	auto hue_color = ImColor::HSV(hue, 1, 1);

	ImGui::GetCurrentWindow()->DrawList->AddLine(
		ImVec2(
			picker_pos.x + (hue * size.x),
			picker_pos.y
		),

		ImVec2(
			picker_pos.x + (hue * size.x),
			picker_pos.y + size.y
		),

		ImColor(255, 255, 255),
		2.f
	);

	bool ret_val = false;
	ImGui::SetCursorScreenPos(ImVec2(picker_pos.x, picker_pos.y));
	ImGui::InvisibleButton("##hue_selector", ImVec2(size.x, size.y));
	if (ImGui::IsItemHovered())
	{
		if (ImGui::GetIO().MouseDown[0])
		{
			hue = ((ImGui::GetIO().MousePos.x - picker_pos.x) / size.x);
			ret_val = true;
			printf("changed\n");
		}
	}

	*col = ImColor::HSV(hue, saturation, value);
	return ret_val;
}

static std::map<ImGuiID, float> combo_times{};
static std::map<ImGuiID, float> combo_draggings{};
bool ImGui::CustomBeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags)
{
	ImGuiContext& g = *GImGui;
	bool has_window_size_constraint = (g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint) != 0;
	g.NextWindowData.Flags &= ~ImGuiNextWindowDataFlags_HasSizeConstraint;

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)); // Can't use both flags together

	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	const float arrow_size = (flags & ImGuiComboFlags_NoArrowButton) ? 0.0f : GetFrameHeight();
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	const float expected_w = 540.f;
	const float w = (flags & ImGuiComboFlags_NoPreview) ? arrow_size : expected_w;
	const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, (label_size.y + style.FramePadding.y) * 1.15f));
	const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id, &frame_bb))
		return false;

	bool hovered, held;
	bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);
	bool popup_open = IsPopupOpen(id, ImGuiPopupFlags_None);

	// Cor de fundo personalizada para todos os estados
	const ImU32 custom_frame_col = color::GetFieldBgU32();

	const float value_x2 = ImMax(frame_bb.Min.x, frame_bb.Max.x - arrow_size);
	RenderNavHighlight(frame_bb, id);

	// Aplicando a cor personalizada no fundo, dependendo do estado (n�o apenas para hover ou pressed)
	if (!(flags & ImGuiComboFlags_NoPreview))
		window->DrawList->AddRectFilled(frame_bb.Min, ImVec2(value_x2, frame_bb.Max.y), custom_frame_col, popup_open ? 0.f : 16.f, (flags & ImGuiComboFlags_NoArrowButton) ? ImDrawCornerFlags_All : ImDrawCornerFlags_Left);

	// O texto permanece branco
	PushStyleColor(ImGuiCol_Text, color::GetModuleTextVec4());

	if (preview_value != NULL && !(flags & ImGuiComboFlags_NoPreview))
		RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y) + style.FramePadding, ImVec2(value_x2, frame_bb.Max.y), preview_value, NULL, NULL, ImVec2(0.0f, 0.0f));

	PopStyleColor(1);

	constexpr auto easeInOutSine = [](float t)
		{
			return (float)(0.5f * (1.f + sinf(3.1415926f * (t - 0.5f))));
		};

	if (combo_times.find(id) == combo_times.end())
		combo_times.emplace(id, 0.f);

	if (combo_draggings.find(id) == combo_draggings.end())
		combo_draggings.emplace(id, false);

	auto& actualTime = combo_times.at(id);
	auto& actualDragging = combo_draggings.at(id);

	if (actualTime >= 2.f)
	{
		actualTime = 0.f;
		popup_open = false;
		actualDragging = false;
	}

	if ((pressed || popup_open) && actualTime < 1.f)
	{
		actualDragging = true;
		actualTime += 0.03f;
		if (actualTime > 1.f)
			actualTime = 1.f;
	}
	if (!popup_open && actualTime >= 1.f)
	{
		actualTime = 0.f;
		actualDragging = false;
	}

	if (((pressed || g.NavActivateId == id) && !popup_open) || actualDragging)
	{
		if (window->DC.NavLayerCurrent == 0)
			window->NavLastIds[0] = id;
		OpenPopupEx(id, ImGuiPopupFlags_None);
		popup_open = true;
	}

	if (actualTime <= 0.f)
		return false;

	if (has_window_size_constraint)
	{
		g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasSizeConstraint;
		g.NextWindowData.SizeConstraintRect.Min.x = ImMax(g.NextWindowData.SizeConstraintRect.Min.x, w);
	}
	else
	{
		auto CalcMaxPopupHeightFromItemCount = [](int items_count)
			{
				ImGuiContext& g = *GImGui;
				if (items_count <= 0)
					return FLT_MAX;
				return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
			};

		if ((flags & ImGuiComboFlags_HeightMask_) == 0)
			flags |= ImGuiComboFlags_HeightRegular;
		IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiComboFlags_HeightMask_));    // Only one
		int popup_max_height_in_items = -1;
		if (flags & ImGuiComboFlags_HeightRegular)     popup_max_height_in_items = 8;
		else if (flags & ImGuiComboFlags_HeightSmall)  popup_max_height_in_items = 4;
		else if (flags & ImGuiComboFlags_HeightLarge)  popup_max_height_in_items = 20;
		SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2{ FLT_MAX, CalcMaxPopupHeightFromItemCount(popup_max_height_in_items) * easeInOutSine(actualTime) });
	}

	char name[16];
	ImFormatString(name, IM_ARRAYSIZE(name), "##Combo_%02d", g.BeginPopupStack.Size); // Recycle windows based on depth

	// Peak into expected window size so we can position it
	if (ImGuiWindow* popup_window = FindWindowByName(name)) {
		if (popup_window->WasActive)
		{
			ImVec2 size_expected = CalcWindowNextAutoFitSize(popup_window);
			if (flags & ImGuiComboFlags_PopupAlignLeft)
				popup_window->AutoPosLastDirection = ImGuiDir_Left;
			ImRect r_outer = GetPopupAllowedExtentRect(popup_window);
			ImVec2 pos = FindBestWindowPosForPopupEx(frame_bb.GetBL(), size_expected, &popup_window->AutoPosLastDirection, r_outer, frame_bb, ImGuiPopupPositionPolicy_ComboBox);
			SetNextWindowPos(pos);
		}
	}

	// We don't use BeginPopupEx() solely because we have a custom name string, which we could make an argument to BeginPopupEx()
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;

	// Horizontally align ourselves with the framed text
	PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 0.f));
	PushStyleColor(ImGuiCol_PopupBg, custom_frame_col);  // Cor personalizada para o fundo
	bool ret = Begin(name, NULL, window_flags);
	PopStyleVar(1);
	PopStyleColor(1);
	if (!ret)
	{
		EndPopup();
		IM_ASSERT(0);   // This should never happen as we tested for IsPopupOpen() above
		return false;
	}
	return true;
}





void ImGui::SpinnerLemniscate(const char* label, float radius, float thickness, const ImColor& color, float speed, float angle)
{
	ImVec2 pos, size, centre; int num_segments;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	pos = window->DC.CursorPos;
	size = ImVec2((radius) * 2, (radius + style.FramePadding.y) * 2);

	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(bb, style.FramePadding.y);

	num_segments = window->DrawList->_CalcCircleAutoSegmentCount(radius);

	centre = bb.GetCenter();
	if (!ImGui::ItemAdd(bb, id))
		return;

	const float start = (float)ImGui::GetTime() * speed;
	const float a = radius;
	const float t = start;
	const float step = angle / num_segments;
	const float th = thickness / num_segments;

	/*
		x = a cos(t) / 1 + sin�(t)
		y = a sin(t) . cos(t) / 1 + sin�(t)
	*/
	const auto get_coord = [&](float const& a, float const& t) -> auto {
		return std::make_pair((a * ImCos(t)) / (1 + (powf(ImSin(t), 2.0f))), (a * ImSin(t) * ImCos(t)) / (1 + (powf(ImSin(t), 2.0f))));
	};

	for (size_t i = 0; i < num_segments; i++)
	{
		const auto xy0 = get_coord(a, start + (i * step));
		const auto xy1 = get_coord(a, start + ((i + 1) * step));

		window->DrawList->AddLine(ImVec2(centre.x + xy0.first, centre.y + xy0.second),
			ImVec2(centre.x + xy1.first, centre.y + xy1.second),
			color,
			th * i);
	}
}

void ImGui::SpinnerCircleDrop(const char* label, float radius, float thickness, float thickness_drop, const ImColor& color, const ImColor& bg, float speed, float angle)
{
	ImVec2 pos, size, centre; int num_segments;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	pos = window->DC.CursorPos;
	size = ImVec2((radius) * 2, (radius + style.FramePadding.y) * 2);

	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(bb, style.FramePadding.y);

	num_segments = window->DrawList->_CalcCircleAutoSegmentCount(radius);

	centre = bb.GetCenter();
	if (!ImGui::ItemAdd(bb, id))
		return;

	window->DrawList->PathClear();
	float start = (float)ImGui::GetTime() * speed;
	const float bg_angle_offset = IM_PI * 2.f / num_segments;

	const float angle_offset = angle / num_segments;
	const float th = thickness_drop / num_segments;
	const float drop_radius_th = thickness_drop / num_segments;
	for (int i = 0; i < num_segments; i++)
	{
		const float a = start + (i * angle_offset);
		const float a1 = start + ((i + 1) * angle_offset);
		const float s_drop_radius = radius - thickness / 2.f - (drop_radius_th * i);
		window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * s_drop_radius, centre.y + ImSin(a) * s_drop_radius),
			ImVec2(centre.x + ImCos(a1) * s_drop_radius, centre.y + ImSin(a1) * s_drop_radius),
			color,
			th * 2.f * i);
	}
	const float ai_end = start + (num_segments * angle_offset);
	const float f_drop_radius = radius - thickness / 2.f - thickness_drop;
	ImVec2 circle_i_center{ centre.x + ImCos(ai_end) * f_drop_radius, centre.y + ImSin(ai_end) * f_drop_radius };
	window->DrawList->AddCircleFilled(circle_i_center, thickness_drop, color, num_segments);

	for (int i = 0; i <= num_segments; i++)
	{
		const float a = (i * bg_angle_offset);
		window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius));
	}
	window->DrawList->PathStroke(bg, false, thickness);
}

void ImGui::SpinnerAngEclipse(const char* label, float radius, float thickness, const ImColor& color, float speed, float angle)
{
	ImVec2 pos, size, centre; int num_segments;
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	pos = window->DC.CursorPos;
	size = ImVec2((radius) * 2, (radius + style.FramePadding.y) * 2);

	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(bb, style.FramePadding.y);

	num_segments = window->DrawList->_CalcCircleAutoSegmentCount(radius);

	centre = bb.GetCenter();
	if (!ImGui::ItemAdd(bb, id))
		return;
	// Render
	float start = (float)ImGui::GetTime() * speed;

	const float angle_offset = angle / num_segments;
	const float th = thickness / num_segments;
	for (size_t i = 0; i < num_segments; i++)
	{
		const float a = start + (i * angle_offset);
		const float a1 = start + ((i + 1) * angle_offset);
		window->DrawList->AddLine(ImVec2(centre.x + ImCos(a) * radius, centre.y + ImSin(a) * radius),
			ImVec2(centre.x + ImCos(a1) * radius, centre.y + ImSin(a1) * radius),
			color,
			th * i);
	}
}
bool ImGui::CustomMultiCombo1(const char* label, std::vector<std::string>& items, int items_size, std::vector<bool>& selectedItems, int height_in_items)
{
	ImGuiContext& g = *GImGui;
	ImGuiID id = ImGui::GetCurrentWindow()->GetID(label);

	const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
	if (label_size.x > 0.0f)
	{
		ImGui::SetCursorPosX(9.f);
		ImGui::Text(label);
		ImGui::SetCursorPosX(9.f);
	}

	// Construindo a string dos itens selecionados
	std::string preview = "";
	int selectedCount = 0;
	for (size_t i = 0; i < items_size; i++)
	{
		if (selectedItems[i])
		{
			if (selectedCount > 0) preview += ", ";
			preview += items[i];
			selectedCount++;
		}
	}
	if (preview.empty()) preview = "Select slots"; // Caso nenhum esteja selecionado

	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_FrameBg)); // Custom border color
	if (!ImGui::CustomBeginCombo(label, preview.c_str(), ImGuiComboFlags_NoArrowButton)) {
		ImGui::PopStyleColor(1);
		return false;
	}

	for (size_t i = 0; i < items_size; i++)
	{
		bool selected = selectedItems[i];
		if (ImGui::Selectable(items[i].c_str(), selected, ImGuiSelectableFlags_DontClosePopups))
		{
			selectedItems[i] = !selected; // Alterna a sele��o
		}
	}

	ImGui::EndCombo();
	ImGui::PopStyleColor(1);
	return true;
}


bool ImGui::CustomCombo1(const char* label, std::vector<std::string>& items, int items_size, int* selectedItem, int height_in_items)
{
	ImGuiContext& g = *GImGui;
	ImGuiID id = GetCurrentWindow()->GetID(label);

	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	if (label_size.x > 0.0f)
	{
		SetCursorPosX(9.f);
		Text(label);
		SetCursorPosX(9.f);
	}

	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_FrameBg)); // cant find where the hell it enable the border
	if (!CustomBeginCombo(label, *selectedItem != -1 && *selectedItem < items.size() ? items[*selectedItem].c_str() : "", ImGuiComboFlags_NoArrowButton)) {
		ImGui::PopStyleColor(1);
		return false;
	}

	for (size_t i = 0; i < items_size; i++)
		if (Selectable(items[i].c_str(), i == *selectedItem))
			*selectedItem = i;

	EndCombo();
	ImGui::PopStyleColor(1);
	return true;
}

#include <iostream>
#include <unordered_map>
#include <map>
#include <chrono>
#include "colors.h"
bool ImGui::Checkbox(const char* label, bool* v) {
	ImGuiWindow* window = GetCurrentWindow();
	ImDrawList* draw = window->DrawList;

	ImGuiContext& g = *GImGui;
	ImGuiStyle& style = g.Style;
	ImGuiID id = window->GetID(label);
	ImVec2 label_size = CalcTextSize(label, NULL, true);

	float w = GetWindowWidth() - 50;
	ImVec2 size = { 40, 25 };

	ImVec2 pos = window->DC.CursorPos;
	ImRect frame_bb(pos + ImVec2(w - size.x, 0), pos + ImVec2(w, size.y));
	ImRect total_bb(pos, pos + ImVec2(w, label_size.y));
	ItemAdd(total_bb, id);
	ItemSize(total_bb, style.FramePadding.y);

	bool hovered, held;
	bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);
	if (pressed) {
		*v = !(*v);
		MarkItemEdited(id);
	}

	static std::unordered_map<ImGuiID, float> values;
	auto value = values.find(id);
	if (value == values.end()) {
		values.insert({ id, 0.f });
		value = values.find(id);
	}

	value->second = ImLerp(value->second, (*v ? 1.6f : 0.f), 0.08f);
	const float square_sz = GetFrameHeight();

	const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));

	RenderText(ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x + 2.5f, check_bb.Min.y + style.FramePadding.y), label);

	draw->AddRectFilled(frame_bb.Min, frame_bb.Max, color::GetFieldBgU32(0.65f), 18);

	draw->AddCircleFilled(ImVec2(frame_bb.Min.x + 8 + (14 * value->second), frame_bb.GetCenter().y), 7, *v ? ImColor(color::GetPurpleVec4()) : ImColor(GetColorU32(ImGuiCol_TextDisabled)), 30);

	return pressed;
}
struct checkbox_element {
	float selected_rect;
};
#include "colors.h"
bool ImGui::Checkbox3(const char* label, bool* v) {
	ImGuiWindow* window = GetCurrentWindow();
	ImDrawList* draw = window->DrawList;

	ImGuiContext& g = *GImGui;
	ImGuiStyle& style = g.Style;
	ImGuiID id = window->GetID(label);
	ImVec2 label_size = CalcTextSize(label, NULL, true);

	float w = GetWindowWidth() - 50;
	ImVec2 size = { 20, 25 };

	ImVec2 pos = window->DC.CursorPos;
	ImRect frame_bb(pos + ImVec2(w - size.x, 0), pos + ImVec2(w, size.y));
	ImRect total_bb(pos, pos + ImVec2(w, label_size.y));
	ItemAdd(total_bb, id);
	ItemSize(total_bb, style.FramePadding.y);

	bool hovered, held;
	bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);
	if (pressed) {
		*v = !(*v);
		MarkItemEdited(id);
	}

	static std::unordered_map<ImGuiID, float> values;
	auto value = values.find(id);
	if (value == values.end()) {
		values.insert({ id, 0.f });
		value = values.find(id);
	}

	value->second = ImLerp(value->second, (*v ? 1.6f : 0.f), 0.08f);
	const float square_sz = GetFrameHeight();

	const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));

	// Render label text
	RenderText(ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x + 2.5f, check_bb.Min.y + style.FramePadding.y), label);

	// Draw the background of the checkbox
	draw->AddRectFilled(frame_bb.Min, frame_bb.Max, color::GetFieldBgU32(0.65f), 18);

	// Alterando a largura (raio na dire��o X) da bola e ajustando a posi��o
	float horizontalRadius = 12.0f;  // Raio horizontal maior
	float verticalRadius = 8.0f;    // Raio vertical (o mesmo)

	float centerX = frame_bb.Min.x + 8;  // Posi��o do centro no eixo X (fixa)
	float centerY = frame_bb.GetCenter().y;  // Posi��o do centro no eixo Y (fixa)

	// Desenha a bola com o raio horizontal maior e o raio vertical normal
	draw->AddCircleFilled(
		ImVec2(centerX, centerY),  // Posi��o do centro
		horizontalRadius,          // Raio horizontal
		*v ? ImColor(color::GetPurpleVec4()) : ImColor(GetColorU32(ImGuiCol_TextDisabled)), // Cor
		20                         // Imagem com 50 segmentos
	);

	return pressed;
}

bool ImGui::Checkbox4(const char* label, bool* v)
{
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);

	const float box_size = 22.0f;
	const float spacing = 10.0f;
	const ImVec2 pos = window->DC.CursorPos;

	ImVec2 total_size = ImVec2(box_size + spacing + label_size.x, ImMax(box_size, label_size.y));
	const ImRect total_bb(pos, pos + total_size);
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id))
		return false;

	bool hovered, held;
	bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
	if (pressed)
	{
		*v = !(*v);
		MarkItemEdited(id);
	}
	static std::map<ImGuiID, checkbox_element> anim;
	auto it_anim = anim.find(id);
	if (it_anim == anim.end())
	{
		anim.insert({ id, { 0.0f } });
		it_anim = anim.find(id);
	}

	it_anim->second.selected_rect = ImLerp(it_anim->second.selected_rect, *v ? 1.0f : (hovered ? 0.25f : 0.0f), 0.15f * ImGui::GetIO().DeltaTime * 60.0f);

	ImColor active_color = ImColor(color::GetPurpleVec4());
	ImColor base_bg = ImColor(color::GetFieldBgVec4(0.65f));
	window->DrawList->AddRectFilled(pos, pos + ImVec2(box_size, box_size), base_bg, 12.0f);
	if (it_anim->second.selected_rect > 0.01f)
	{
		window->DrawList->AddRectFilled(
			pos,
			pos + ImVec2(box_size, box_size),
			ImColor(active_color.Value.x, active_color.Value.y, active_color.Value.z, it_anim->second.selected_rect),
			12.0f
		);
	}
	if (*v || it_anim->second.selected_rect > 0.05f)
	{
		RenderCheckMark4(
			window->DrawList,
			pos + ImVec2(4, box_size / 2 - 5),
			ImColor(color::GetBackgroundVec4(it_anim->second.selected_rect)),
			12.0f
		);
	}
	if (label[0] != '#' || label[1] != '#')
	{
		ImVec2 label_pos = pos + ImVec2(box_size + spacing, (box_size - label_size.y) * 0.5f);
		window->DrawList->AddText(label_pos, GetColorU32(ImGuiCol_Text), label);
	}

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.ItemFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
	return pressed;
}







void ImGui::CustomColorPicker(const char* name, float color[4], ImVec2 size) noexcept
{
	ImGui::PushID(name);

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));

	ImVec4 colorPreview(color[0], color[1], color[2], color[3]);
	bool openPopup = ImGui::ColorButton(name, colorPreview,
		ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview, size);
	ImGui::PopStyleVar(2);

	static float popupAlpha = 0.0f;
	static bool active = false;

	if (openPopup)
	{
		active = true;
		ImGui::OpenPopup("##popup");
	}

	if (active)
		popupAlpha = ImClamp(popupAlpha + ImGui::GetIO().DeltaTime * 10.0f, 0.0f, 1.0f);
	else
		popupAlpha = ImClamp(popupAlpha - ImGui::GetIO().DeltaTime * 10.0f, 0.0f, 1.0f);

	ImGui::SetNextWindowBgAlpha(popupAlpha);

	if (ImGui::BeginPopup("##popup", ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (popupAlpha < 0.01f)
			ImGui::CloseCurrentPopup();

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, color::GetPanelVec4(0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border, color::GetBorderVec4());

		ImGui::SetNextItemWidth(250);

		ImVec4 buttonBgColor = color::GetFieldBgVec4();
		ImGui::PushStyleColor(ImGuiCol_Button, buttonBgColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonBgColor.x + 0.1f, buttonBgColor.y + 0.1f, buttonBgColor.z + 0.1f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonBgColor.x + 0.15f, buttonBgColor.y + 0.15f, buttonBgColor.z + 0.15f, 1.0f));

		ImGui::ColorPicker4("##picker", color,
			ImGuiColorEditFlags_NoTooltip |
			ImGuiColorEditFlags_DisplayRGB |
			ImGuiColorEditFlags_NoSidePreview |
			ImGuiColorEditFlags_NoLabel |
			ImGuiColorEditFlags_AlphaBar);

		ImGui::PopStyleColor(3);

		static char hexInput[9] = "00000000";

		if (!ImGui::IsItemActive() && !ImGui::IsAnyItemActive())
		{
			snprintf(hexInput, sizeof(hexInput), "%02X%02X%02X%02X",
				(int)(color[0] * 255.0f),
				(int)(color[1] * 255.0f),
				(int)(color[2] * 255.0f),
				(int)(color[3] * 255.0f));
		}

		float fullWidth = ImGui::GetContentRegionAvail().x;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, color::GetPanelVec4(0.82f));
		ImGui::BeginChild("##hex_bg", ImVec2(fullWidth, 30), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		// Aumenta o padding vertical para aumentar a altura do InputText
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 8.0f)); // 8.0f no y para maior altura

		ImGui::PushStyleColor(ImGuiCol_FrameBg, color::GetFieldBgVec4());
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color::GetFieldHoverVec4());
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color::GetFieldActiveVec4());

		ImGui::PushItemWidth(fullWidth);

		ImGui::SetCursorPosX(0);

		if (ImGui::InputText("##hexinput", hexInput, sizeof(hexInput),
			ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase))
		{
			unsigned int r = 0, g = 0, b = 0, a = 255;
			int result = sscanf(hexInput, "%02X%02X%02X%02X", &r, &g, &b, &a);
			if (result == 4 || result == 3) {
				color[0] = r / 255.0f;
				color[1] = g / 255.0f;
				color[2] = b / 255.0f;
				if (result == 4)
					color[3] = a / 255.0f;
			}
		}

		ImGui::PopItemWidth();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(); // FramePadding aumentado

		ImGui::EndChild();
		ImGui::PopStyleColor(); // ChildBg

		if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
			!ImGui::IsAnyItemActive() &&
			ImGui::IsMouseClicked(0))
		{
			active = false;
		}

		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
		ImGui::EndPopup();
	}
	else
	{
		active = false;
	}

	ImGui::PopID();
}













#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <windows.h>

const char* const KeyNames[] = {
	"Unknown",
	"LBUTTON",
	"RBUTTON",
	"CANCEL",
	"MBUTTON",
	"XBUTTON1",
	"XBUTTON2",
	"Unknown",
	"BACK",
	"TAB",
	"Unknown",
	"Unknown",
	"CLEAR",
	"RETURN",
	"Unknown",
	"Unknown",
	"SHIFT",
	"CONTROL",
	"MENU",
	"PAUSE",
	"CAPITAL",
	"KANA",
	"Unknown",
	"JUNJA",
	"FINAL",
	"KANJI",
	"Unknown",
	"ESCAPE",
	"CONVERT",
	"NONCONVERT",
	"ACCEPT",
	"MODECHANGE",
	"SPACE",
	"PRIOR",
	"NEXT",
	"END",
	"HOME",
	"LEFT",
	"UP",
	"RIGHT",
	"DOWN",
	"SELECT",
	"PRINT",
	"EXECUTE",
	"SNAPSHOT",
	"INSERT",
	"DELETE",
	"HELP",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"A",
	"B",
	"C",
	"D",
	"E",
	"F",
	"G",
	"H",
	"I",
	"J",
	"K",
	"L",
	"M",
	"N",
	"O",
	"P",
	"Q",
	"R",
	"S",
	"T",
	"U",
	"V",
	"W",
	"X",
	"Y",
	"Z",
	"LWIN",
	"RWIN",
	"APPS",
	"N/A",
	"SLEEP",
	"N0",
	"N1",
	"N2",
	"N3",
	"N4",
	"N5",
	"N6",
	"N7",
	"N8",
	"N9",
	"MULTIPLY",
	"ADD",
	"SEPARATOR",
	"SUBTRACT",
	"DECIMAL",
	"DIVIDE",
	"F1",
	"F2",
	"F3",
	"F4",
	"F5",
	"F6",
	"F7",
	"F8",
	"F9",
	"F10",
	"F11",
	"F12",
	"F13",
	"F14",
	"F15",
	"F16",
	"F17",
	"F18",
	"F19",
	"F20",
	"F21",
	"F22",
	"F23",
	"F24",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"NUMLOCK",
	"SCROLL",
	"OEM_NEC_EQUAL",
	"OEM_FJ_MASSHOU",
	"OEM_FJ_TOUROKU",
	"OEM_FJ_LOYA",
	"OEM_FJ_ROYA",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"N/A",
	"LSHIFT",
	"RSHIFT",
	"LCONTROL",
	"RCONTROL",
	"LMENU",
	"RMENU"
};

bool ImGui::Hotkey(const char* label, int* k, const ImVec2& size_arg)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	ImGuiIO& io = g.IO;
	ImGuiStyle& style = g.Style;
	float lastborder = style.FrameBorderSize;
	style.FrameBorderSize = 0.f;

	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), label_size.y + style.FramePadding.y * 2.0f);
	const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + size);
	const ImRect total_bb(window->DC.CursorPos, frame_bb.Max);

	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id, NULL, 1 << 0))
		return false;


	const bool focus_requested = (GetItemStatusFlags() & ImGuiItemStatusFlags_FocusedByTabbing) != 0;
	const bool focus_requested_by_code = focus_requested;
	const bool focus_requested_by_tab = focus_requested && !focus_requested_by_code;

	const bool hovered = ItemHoverable(frame_bb, id);

	if (hovered) {
		SetHoveredID(id);
		g.MouseCursor = ImGuiMouseCursor_TextInput;
	}

	const bool user_clicked = hovered && io.MouseClicked[0];

	if (focus_requested || user_clicked) {
		if (g.ActiveId != id) {
			// Começa a ler a bind
			memset(io.MouseDown, 0, sizeof(io.MouseDown));
			memset(io.KeysDown, 0, sizeof(io.KeysDown));
			// Clear stale scroll flags to prevent accidental scroll capture
			g_ScrollUpTriggered = false;
			g_ScrollDownTriggered = false;
			*k = 0;
			// Set cooldown so scroll from the same physical click isn't captured
			SetHotkeyActivatedTime();
		}
		SetActiveID(id, window);
		FocusWindow(window);
	}
	else if (io.MouseClicked[0]) {
		// Tira o foco se clicar fora
		if (g.ActiveId == id)
			ClearActiveID();
	}

	bool value_changed = false;
	int key = *k;

	if (g.ActiveId == id) {
		for (auto i = 0; i < 5; i++) {
			if (io.MouseDown[i]) {
				switch (i) {
				case 0:
					key = VK_LBUTTON;
					break;
				case 1:
					key = VK_RBUTTON;
					break;
				case 2:
					key = VK_MBUTTON;
					break;
				case 3:
					key = VK_XBUTTON1;
					break;
				case 4:
					key = VK_XBUTTON2;
					break;
				}
				value_changed = true;
				ImGui::ClearActiveID();
			}
		}
		if (!value_changed && g_ScrollUpTriggered) {
			g_ScrollUpTriggered = false;
			key = 0xFE; // SCROLL UP
			value_changed = true;
			ClearActiveID();
		}
		if (!value_changed && g_ScrollDownTriggered) {
			g_ScrollDownTriggered = false;
			key = 0xFF; // SCROLL DOWN
			value_changed = true;
			ClearActiveID();
		}
		if (!value_changed) {
			for (auto i = VK_BACK; i <= VK_RMENU; i++) {
				if (io.KeysDown[i]) {
					key = i;
					value_changed = true;
					ClearActiveID();
				}
			}
		}

		if (IsKeyPressedMap(ImGuiKey_Escape)) {
			*k = 0;
			ClearActiveID();
		}
		else {
			*k = key;
		}
	}

	char buf_display[64] = "None";
	ImDrawList* dl = ImGui::GetWindowDrawList();

	bool typing = g.ActiveId == id;

	if (*k != 0 && g.ActiveId != id)
	{
		char test[256];
		if (*k == 0xFE)
			sprintf_s(test, "SCROLL UP");
		else if (*k == 0xFF)
			sprintf_s(test, "SCROLL DN");
		else if (*k == VK_LBUTTON)
			strcpy_s(test, "LMB");
		else if (*k == VK_RBUTTON)
			strcpy_s(test, "RMB");
		else if (*k == VK_MBUTTON)
			strcpy_s(test, "MMB");
		else if (*k <= 0xA5)
			sprintf_s(test, "%s", KeyNames[*k]);
		else
			sprintf_s(test, "0x%02X", *k);
		strcpy_s(buf_display, test);
	}
	else if (typing)
		strcpy_s(buf_display, "Key...");

	// Padding interno para o texto não colar nas bordas
	const float textPadX = 6.f;
	ImRect textClipRect(frame_bb.Min.x + textPadX, frame_bb.Min.y + 1.f,
		frame_bb.Max.x - textPadX, frame_bb.Max.y - 1.f);

	ImVec2 textSize = CalcTextSize(buf_display, NULL, false);
	ImVec2 textPos = {
		frame_bb.Min.x + style.FramePadding.x + ((size.x - style.FramePadding.x * 2.f) - textSize.x) * 0.5f,
		frame_bb.Min.y + (size.y - textSize.y) * 0.5f
	};

	// Clamp horizontal para garantir que o texto não ultrapasse as bordas
	if (textPos.x < textClipRect.Min.x) textPos.x = textClipRect.Min.x;

	// Draw frame with state-aware styling
	if (typing) {
		dl->AddRectFilled(frame_bb.Min, frame_bb.Max, color::GetFieldActiveU32(0.96f), 14.f);
		dl->AddRect(frame_bb.Min, frame_bb.Max, color::GetAccentU32(0.96f), 14.f, 0, 1.5f);
	} else if (hovered) {
		dl->AddRectFilled(frame_bb.Min, frame_bb.Max, color::GetFieldHoverU32(0.94f), 14.f);
		dl->AddRect(frame_bb.Min, frame_bb.Max, color::GetBorderU32(0.90f), 14.f, 0, 1.f);
	} else {
		dl->AddRectFilled(frame_bb.Min, frame_bb.Max, color::GetFieldBgU32(0.88f), 14.f);
		dl->AddRect(frame_bb.Min, frame_bb.Max, color::GetBorderU32(0.76f), 14.f, 0, 0.75f);
	}

	RenderTextClipped(textPos, textClipRect.Max, buf_display, NULL, &textClipRect.Max, style.ButtonTextAlign);

	// Blinking caret when typing
	if (typing) {
		long long caret_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		bool caretOn = (caret_ms / 530) % 2 == 0;
		if (caretOn) {
			ImVec2 caretStart(textPos.x + textSize.x + 2.f, textPos.y);
			ImVec2 caretEnd(caretStart.x, textPos.y + textSize.y);
			dl->AddLine(caretStart, caretEnd, color::GetAccentU32(0.84f), 1.2f);
		}
	}

	if (label_size.x > 0)
		if (IsItemHovered())
			SetTooltip(label);

	style.FrameBorderSize = lastborder;
	return value_changed;

}
