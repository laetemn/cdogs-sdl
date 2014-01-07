/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (C) 1995 Ronny Wester
    Copyright (C) 2003 Jeremy Chin
    Copyright (C) 2003 Lucas Martin-King

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    This file incorporates work covered by the following copyright and
    permission notice:

    Copyright (c) 2013-2014, Cong Xu
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <SDL.h>

#include <cdogs/actors.h>
#include <cdogs/automap.h>
#include <cdogs/config.h>
#include <cdogs/draw.h>
#include <cdogs/drawtools.h>
#include <cdogs/events.h>
#include <cdogs/files.h>
#include <cdogs/grafx.h>
#include <cdogs/keyboard.h>
#include <cdogs/mission.h>
#include <cdogs/objs.h>
#include <cdogs/palette.h>
#include <cdogs/pic_manager.h>
#include <cdogs/text.h>
#include <cdogs/triggers.h>
#include <cdogs/utils.h>

#include "charsed.h"
#include "editor_ui.h"
#include "ui_object.h"


#define TH  8


// Mouse click areas:
static UIObject *sObjs;


// Globals

Mission *currentMission;
static char lastFile[CDOGS_FILENAME_MAX];



// Code...

void DisplayCDogsText(int x, int y, const char *text, int hilite, int editable)
{
	CDogsTextGoto(x, y);
	if (editable) {
		if (hilite)
			CDogsTextCharWithTable('\020', &tableFlamed);
		else
			CDogsTextChar('\020');
	}

	if (hilite && !editable)
		CDogsTextStringWithTable(text, &tableFlamed);
	else
		CDogsTextString(text);

	if (editable) {
		if (hilite)
			CDogsTextCharWithTable('\021', &tableFlamed);
		else
			CDogsTextChar('\021');
	}
}

static void DrawEditableTextWithEmptyHint(
	Vec2i pos, char *text, char *hint, int isHighlighted)
{
	int isEmptyText = strlen(text) == 0;
	color_t bracketMask = isHighlighted ? colorRed : colorWhite;
	color_t textMask = isEmptyText ? colorGray : colorWhite;
	if (isEmptyText)
	{
		text = hint;
	}
	pos = DrawTextCharMasked('\020', &gGraphicsDevice, pos, bracketMask);
	pos = DrawTextStringMasked(text, &gGraphicsDevice, pos, textMask);
	pos = DrawTextCharMasked('\021', &gGraphicsDevice, pos, bracketMask);
}

static Vec2i GetMouseTile(GraphicsDevice *g, EventHandlers *e)
{
	int w = g->cachedConfig.ResolutionWidth;
	int h = g->cachedConfig.ResolutionHeight;
	Vec2i mapSize = Vec2iNew(
		currentMission->Size.x * TILE_WIDTH,
		currentMission->Size.y * TILE_HEIGHT);
	Vec2i mapPos = Vec2iNew((w - mapSize.x) / 2, (h - mapSize.y) / 2);
	return Vec2iNew(
		(e->mouse.currentPos.x - mapPos.x) / TILE_WIDTH,
		(e->mouse.currentPos.y - mapPos.y) / TILE_HEIGHT);
}

static void SwapCursorTile(Vec2i mouseTile)
{
	static Vec2i cursorTilePos = { -1, -1 };
	static Tile cursorTile;
	Tile *t;

	// Convert the tile coordinates to map tile coordinates
	// The map is centered, i.e. edges are empty
	// TODO: refactor map to use clearer coordinates
	mouseTile.x += (XMAX - currentMission->Size.x) / 2;
	mouseTile.y += (YMAX - currentMission->Size.y) / 2;

	// Draw the cursor tile by replacing it with the map tile at the
	// cursor position
	// If moving to a new tile, restore the last tile,
	// and swap with the new tile
	if (cursorTilePos.x >= 0 && cursorTilePos.y >= 0)
	{
		// restore
		memcpy(
			MapGetTile(&gMap, cursorTilePos),
			&cursorTile,
			sizeof cursorTile);
	}
	// swap
	cursorTilePos = mouseTile;
	t = MapGetTile(&gMap, cursorTilePos);
	memcpy(&cursorTile, t, sizeof cursorTile);
	// Set cursor tile properties
	t->pic = PicManagerGetFromOld(
		&gPicManager, cWallPics[currentMission->WallStyle][WALL_SINGLE]);
	t->picAlt = picNone;
	t->flags = MAPTILE_IS_WALL;
	t->isVisited = 1;
	t->things = NULL;
}

static void MakeBackground(
	GraphicsDevice *g, GraphicsConfig *config, int mission)
{
	int i;
	// Clear background first
	for (i = 0; i < GraphicsGetScreenSize(&g->cachedConfig); i++)
	{
		g->buf[i] = PixelFromColor(g, colorBlack);
	}
	GrafxMakeBackground(g, config, tintDarker, mission, 1);
}

static void Display(int mission, int yc, int willDisplayAutomap)
{
	char s[128];
	int y = 5;
	int i;
	int w = gGraphicsDevice.cachedConfig.ResolutionWidth;
	int h = gGraphicsDevice.cachedConfig.ResolutionHeight;

	if (currentMission)
	{
		Vec2i mouseTile = GetMouseTile(&gGraphicsDevice, &gEventHandlers);
		int isMouseTileValid =
			mouseTile.x >= 0 && mouseTile.x < currentMission->Size.x &&
			mouseTile.y >= 0 && mouseTile.y < currentMission->Size.y;
		// Re-make the background if the resolution has changed
		if (gEventHandlers.HasResolutionChanged)
		{
			MakeBackground(&gGraphicsDevice, &gConfig.Graphics, mission);
		}
		if (isMouseTileValid)
		{
			SwapCursorTile(mouseTile);
			GrafxDrawBackground(
				&gGraphicsDevice, &gConfig.Graphics, tintDarker);
		}
		GraphicsBlitBkg(&gGraphicsDevice);
		sprintf(
			s, "Mission %d/%d", mission + 1, gCampaign.Setting.Missions.size);
		DrawTextStringMasked(
			s, &gGraphicsDevice, Vec2iNew(270, y),
			yc == YC_MISSIONINDEX ? colorRed : colorWhite);
		if (isMouseTileValid)
		{
			sprintf(s, "(%d, %d)", mouseTile.x, mouseTile.y);
			DrawTextString(s, &gGraphicsDevice, Vec2iNew(w - 40, h - 16));
		}
	}
	else
	{
		for (i = 0; i < GraphicsGetScreenSize(&gGraphicsDevice.cachedConfig); i++)
		{
			gGraphicsDevice.buf[i] = LookupPalette(58);
		}
		if (gCampaign.Setting.Missions.size)
		{
			sprintf(s, "End/%d", gCampaign.Setting.Missions.size);
			DrawTextStringMasked(
				s, &gGraphicsDevice, Vec2iNew(270, y),
				yc == YC_MISSIONINDEX ? colorRed : colorWhite);
		}
	}

	if (fileChanged)
	{
		DrawTPic(10, y, PicManagerGetOldPic(&gPicManager, 221));
	}

	DrawTextString(
		"Press F1 for help",
		&gGraphicsDevice,
		Vec2iNew(20, h - 20 - CDogsTextHeight()));

	y = 150;

	UIObjectDraw(sObjs, &gGraphicsDevice);

	if (willDisplayAutomap && currentMission)
	{
		AutomapDraw(AUTOMAP_FLAGS_SHOWALL);
	}
	else
	{
		UIObject *o;
		if (UITryGetObject(sObjs, gEventHandlers.mouse.currentPos, &o) &&
			o->Tooltip)
		{
			Vec2i tooltipPos = Vec2iAdd(
				gEventHandlers.mouse.currentPos, Vec2iNew(10, 10));
			DrawTooltip(&gGraphicsDevice, tooltipPos, o->Tooltip);
		}
		MouseDraw(&gEventHandlers.mouse);
	}
	BlitFlip(&gGraphicsDevice, &gConfig.Graphics);
}

static int Change(UIObject *o, int yc, int d, int *mission)
{
	int isChanged = 0;

	if (yc == YC_MISSIONINDEX) {
		*mission += d;
		*mission = CLAMP(*mission, 0, (int)gCampaign.Setting.Missions.size);
		return 0;
	}

	if (!currentMission)
		return 0;

	if (o && o->ChangeFunc)
	{
		o->ChangeFunc(o->Data, d);
		isChanged = 1;
	}
	return isChanged;
}

static void InsertMission(int idx, Mission *mission)
{
	if (mission)
	{
		CArrayInsert(&gCampaign.Setting.Missions, idx, mission);
	}
	else
	{
		Mission defaultMission;
		MissionInit(&defaultMission);
		defaultMission.Size = Vec2iNew(48, 48);
		CArrayInsert(&gCampaign.Setting.Missions, idx, &defaultMission);
	}
}

static void DeleteMission(int *idx)
{
	if (*idx >= (int)gCampaign.Setting.Missions.size)
	{
		return;
	}
	MissionTerminate(CArrayGet(&gCampaign.Setting.Missions, *idx));
	CArrayDelete(&gCampaign.Setting.Missions, *idx);
	if (gCampaign.Setting.Missions.size > 0 &&
		*idx >= (int)gCampaign.Setting.Missions.size)
	{
		*idx = gCampaign.Setting.Missions.size - 1;
	}
}

static void AddObjective(Mission *m)
{
	// TODO: support more objectives
	if (m->Objectives.size < OBJECTIVE_MAX_OLD)
	{
		MissionObjective mo;
		memset(&mo, 0, sizeof mo);
		CArrayPushBack(&m->Objectives, &mo);
	}
}

static void DeleteObjective(Mission *m, int idx)
{
	CArrayDelete(&m->Objectives, idx);
}

static void DeleteCharacter(Mission *m, int idx)
{
	CArrayDelete(&m->Enemies, idx);
	CharacterStoreDeleteBaddie(&gCampaign.Setting.characters, idx);
}

static void DeleteSpecial(Mission *m, int idx)
{
	CArrayDelete(&m->SpecialChars, idx);
	CharacterStoreDeleteSpecial(&gCampaign.Setting.characters, idx);
}

static void DeleteItem(Mission *m, int idx)
{
	CArrayDelete(&m->Items, idx);
	CArrayDelete(&m->ItemDensities, idx);
}

static void Append(char *s, int maxlen, char c)
{
	size_t l = strlen(s);

	if ((int)l < maxlen)
	{
		s[l + 1] = 0;
		s[l] = c;
	}
}
static void Expand(char *s, char c)
{
	size_t l = strlen(s);
	CREALLOC(s, l + 1);
	s[l + 1] = 0;
	s[l] = c;
}

static void Backspace(char *s)
{
	if (s[0])
		s[strlen(s) - 1] = 0;
}

static void AddChar(int xc, int yc, char c)
{
	if (yc == YC_CAMPAIGNTITLE) {
		switch (xc) {
		case XC_CAMPAIGNTITLE:
			Expand(gCampaign.Setting.Title, c);
			break;
		case XC_AUTHOR:
			Expand(gCampaign.Setting.Author, c);
			break;
		case XC_CAMPAIGNDESC:
			Expand(gCampaign.Setting.Description, c);
			break;
		}
	}

	if (!currentMission)
		return;

	switch (yc) {
	case YC_MISSIONTITLE:
		if (xc == XC_MUSICFILE)
		{
			Append(currentMission->Song, sizeof currentMission->Song - 1, c);
		}
		else
		{
			Expand(currentMission->Title, c);
		}
		break;

	case YC_MISSIONDESC:
		Expand(currentMission->Description, c);
		break;

	default:
		if (yc - YC_OBJECTIVES < (int)currentMission->Objectives.size)
		{
			MissionObjective *mobj =
				CArrayGet(&currentMission->Objectives, yc - YC_OBJECTIVES);
			Expand(mobj->Description, c);
		}
		break;
	}
}

static void DelChar(int xc, int yc)
{
	if (yc == YC_CAMPAIGNTITLE) {
		switch (xc) {
		case XC_CAMPAIGNTITLE:
			Backspace(gCampaign.Setting.Title);
			break;
		case XC_AUTHOR:
			Backspace(gCampaign.Setting.Author);
			break;
		case XC_CAMPAIGNDESC:
			Backspace(gCampaign.Setting.Description);
			break;
		}
	}

	if (!currentMission)
		return;

	switch (yc) {
	case YC_MISSIONTITLE:
		if (xc == XC_MUSICFILE)
		{
			Backspace(currentMission->Song);
		}
		else
		{
			Backspace(currentMission->Title);
		}
		break;

	case YC_MISSIONDESC:
		Backspace(currentMission->Description);
		break;

	default:
		if (yc - YC_OBJECTIVES < (int)currentMission->Objectives.size)
		{
			MissionObjective *mobj =
				CArrayGet(&currentMission->Objectives, yc - YC_OBJECTIVES);
			Backspace(mobj->Description);
		}
		break;
	}
}

static void AdjustYC(int *yc)
{
	if (currentMission != NULL)
	{
		if (currentMission->Objectives.size)
		{
			*yc = CLAMP_OPPOSITE(
				*yc, 0, YC_OBJECTIVES + (int)currentMission->Objectives.size - 1);
		}
		else
		{
			*yc = CLAMP_OPPOSITE(*yc, 0, YC_OBJECTIVES);
		}
	}
	else
	{
		*yc = CLAMP_OPPOSITE(*yc, 0, YC_MISSIONINDEX);
	}
}

static void AdjustXC(int yc, int *xc)
{
	switch (yc)
	{
	case YC_CAMPAIGNTITLE:
		*xc = CLAMP_OPPOSITE(*xc, 0, XC_CAMPAIGNDESC);
		break;

	case YC_MISSIONTITLE:
		*xc = CLAMP_OPPOSITE(*xc, 0, XC_MUSICFILE);
		break;

	case YC_MISSIONPROPS:
		*xc = CLAMP_OPPOSITE(*xc, 0, XC_DENSITY);
		break;

	case YC_MISSIONLOOKS:
		*xc = CLAMP_OPPOSITE(*xc, 0, XC_COLOR4);
		break;

	case YC_CHARACTERS:
		if (currentMission && currentMission->Enemies.size > 0)
		{
			*xc = CLAMP_OPPOSITE(*xc, 0, (int)currentMission->Enemies.size - 1);
		}
		break;

	case YC_SPECIALS:
		if (currentMission && currentMission->SpecialChars.size > 0)
		{
			*xc = CLAMP_OPPOSITE(*xc, 0, (int)currentMission->SpecialChars.size - 1);
		}
		break;

	case YC_ITEMS:
		if (currentMission && currentMission->Items.size > 0)
		{
			*xc = CLAMP_OPPOSITE(*xc, 0, (int)currentMission->Items.size - 1);
		}
		break;

	case YC_WEAPONS:
		*xc = CLAMP_OPPOSITE(*xc, 0, XC_MAXWEAPONS);
		break;

	default:
		if (yc >= YC_OBJECTIVES)
		{
			*xc = CLAMP_OPPOSITE(*xc, 0, XC_FLAGS);
		}
		break;
	}
}

static void MoveSelection(int isForward, int *y, int *x)
{
	if (isForward)
	{
		(*y)++;
	}
	else
	{
		(*y)--;
	}
	AdjustYC(y);
	AdjustXC(*y, x);
}

static void Setup(int idx, int buildTables)
{
	if (idx >= (int)gCampaign.Setting.Missions.size)
	{
		currentMission = NULL;
		return;
	}
	MissionOptionsTerminate(&gMission);
	currentMission = CArrayGet(&gCampaign.Setting.Missions, idx);
	SetupMission(idx, buildTables, &gCampaign);
	MakeBackground(&gGraphicsDevice, &gConfig.Graphics, idx);
}

static void Open(void)
{
	char filename[CDOGS_FILENAME_MAX];
	int c;
	
	strcpy(filename, lastFile);
	for (;;)
	{
		int i;
		for (i = 0; i < GraphicsGetScreenSize(&gGraphicsDevice.cachedConfig); i++)
		{
			gGraphicsDevice.buf[i] = LookupPalette(58);
		}
		CDogsTextStringAt(125, 50, "Open file:");
		CDogsTextGoto(125, 50 + CDogsTextHeight());
		CDogsTextChar('\020');
		CDogsTextString(filename);
		CDogsTextChar('\021');
		BlitFlip(&gGraphicsDevice, &gConfig.Graphics);
		
		c = GetKey(&gEventHandlers);
		switch (c)
		{
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				if (!filename[0])
					break;
				CampaignSettingTerminate(&gCampaign.Setting);
				CampaignSettingInit(&gCampaign.Setting);
				if (LoadCampaignOld(filename, &gCampaign.Setting))
				{
					printf("Error: cannot load %s\n", lastFile);
					continue;
				}
				Setup(0, 1);
				fileChanged = 0;
				strcpy(lastFile, filename);
				return;
				
			case SDLK_ESCAPE:
				return;
				
			case SDLK_BACKSPACE:
				if (filename[0])
					filename[strlen(filename) - 1] = 0;
				break;
				
			default:
				if (strlen(filename) == sizeof(filename) - 1)
				{
					break;
				}
				c = KeyGetTyped(&gEventHandlers.keyboard);
				if (c && c != '*' &&
					(strlen(filename) > 1 || c != '-') &&
					c != ':' && c != '<' && c != '>' && c != '?' &&
					c != '|')
				{
					size_t si = strlen(filename);
					filename[si + 1] = 0;
					filename[si] = (char)c;
				}
		}
		SDL_Delay(10);
	}
}

static void Save(void)
{
	char filename[CDOGS_FILENAME_MAX];
	int c;

	strcpy(filename, lastFile);
	for (;;)
	{
		int i;
		for (i = 0; i < GraphicsGetScreenSize(&gGraphicsDevice.cachedConfig); i++)
		{
			gGraphicsDevice.buf[i] = LookupPalette(58);
		}
		CDogsTextStringAt(125, 50, "Save as:");
		CDogsTextGoto(125, 50 + CDogsTextHeight());
		CDogsTextChar('\020');
		CDogsTextString(filename);
		CDogsTextChar('\021');
		BlitFlip(&gGraphicsDevice, &gConfig.Graphics);

		c = GetKey(&gEventHandlers);
		switch (c)
		{
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			if (!filename[0])
			{
				break;
			}
			MapNewSave(filename, &gCampaign.Setting);
			fileChanged = 0;
			strcpy(lastFile, filename);
			return;

		case SDLK_ESCAPE:
			return;

		case SDLK_BACKSPACE:
			if (filename[0])
				filename[strlen(filename) - 1] = 0;
			break;

		default:
			if (strlen(filename) == sizeof(filename) - 1)
			{
				break;
			}
			c = KeyGetTyped(&gEventHandlers.keyboard);
			if (c && c != '*' &&
				(strlen(filename) > 1 || c != '-') &&
				c != ':' && c != '<' && c != '>' && c != '?' &&
				c != '|')
			{
				size_t si = strlen(filename);
				filename[si + 1] = 0;
				filename[si] = (char)c;
			}
		}
		SDL_Delay(10);
	}
}

static int ConfirmClose(char *msg)
{
	int c;
	int i;
	int w = gGraphicsDevice.cachedConfig.ResolutionWidth;
	int h = gGraphicsDevice.cachedConfig.ResolutionHeight;
	const char *s1 = "Campaign has been modified, but not saved";
	for (i = 0; i < GraphicsGetScreenSize(&gGraphicsDevice.cachedConfig); i++)
	{
		gGraphicsDevice.buf[i] = LookupPalette(58);
	}
	DrawTextString(
		s1,
		&gGraphicsDevice,
		Vec2iNew((w - TextGetStringWidth(s1)) / 2, (h - CDogsTextHeight()) / 2));
	DrawTextString(
		msg,
		&gGraphicsDevice,
		Vec2iNew((w - TextGetStringWidth(msg)) / 2, (h + CDogsTextHeight()) / 2));
	BlitFlip(&gGraphicsDevice, &gConfig.Graphics);

	c = GetKey(&gEventHandlers);
	return (c == 'Y' || c == 'y');
}

static void HelpScreen(void)
{
	Vec2i pos = Vec2iNew(20, 20);
	const char *helpText =
		"Help\n"
		"====\n"
		"Use mouse to select controls; keyboard to type text\n"
		"Open files by dragging them over the editor shortcut\n"
		"\n"
		"Common commands\n"
		"===============\n"
		"left/right click, page up/down: Increase/decrease value\n"
		"shift + left/right click:       Increase/decrease number of items\n"
		"insert:                         Add new item\n"
		"delete:                         Delete selected item\n"
		"\n"
		"Other commands\n"
		"==============\n"
		"Escape:                         Back or quit\n"
		"Ctrl+E:                         Go to character editor\n"
		"Ctrl+N:                         New mission or character\n"
		"Ctrl+O:                         Open file\n"
		"Ctrl+S:                         Save file\n"
		"Ctrl+X, C, V:                   Cut/copy/paste\n"
		"Ctrl+M:                         Preview automap\n"
		"F1:                             This screen\n";
	int i;
	for (i = 0; i < GraphicsGetScreenSize(&gGraphicsDevice.cachedConfig); i++)
	{
		gGraphicsDevice.buf[i] = LookupPalette(58);
	}
	DrawTextString(helpText, &gGraphicsDevice, pos);
	BlitFlip(&gGraphicsDevice, &gConfig.Graphics);
	GetKey(&gEventHandlers);
}

static void Delete(int xc, int yc, int *mission)
{
	switch (yc)
	{
	case YC_CHARACTERS:
		DeleteCharacter(currentMission, xc);
		break;

	case YC_SPECIALS:
		DeleteSpecial(currentMission, xc);
		break;

	case YC_ITEMS:
		DeleteItem(currentMission, xc);
		break;

	default:
		if (yc >= YC_OBJECTIVES)
		{
			DeleteObjective(currentMission, yc - YC_OBJECTIVES);
		}
		else
		{
			DeleteMission(mission);
		}
		AdjustYC(&yc);
		break;
	}
	fileChanged = 1;
	Setup(*mission, 0);
}

static void HandleInput(
	int c, int m,
	int *xc, int *yc, int *xcOld, int *ycOld,
	int *mission, Mission *scrap,
	int *willDisplayAutomap, int *done)
{
	UIObject *o = NULL;
	if (m)
	{
		if (UITryGetObject(sObjs, gEventHandlers.mouse.currentPos, &o))
		{
			UIObjectHighlight(o);
			*xcOld = *xc;
			*ycOld = *yc;
			// Only change selection on left/right click
			if (m == SDL_BUTTON_LEFT || m == SDL_BUTTON_RIGHT)
			{
				if (!(o->Flags & UI_LEAVE_YC))
				{
					*yc = o->Id;
					AdjustYC(yc);
				}
				if (!(o->Flags & UI_LEAVE_XC))
				{
					*xc = o->Id2;
					AdjustXC(*yc, xc);
				}
			}
			if (!(o->Flags & UI_SELECT_ONLY) &&
				(!(o->Flags & UI_SELECT_ONLY_FIRST) || (*xc == *xcOld && *yc == *ycOld)))
			{
				if (m == SDL_BUTTON_LEFT || m == SDL_BUTTON_WHEELUP)
				{
					c = SDLK_PAGEUP;
				}
				else if (m == SDL_BUTTON_RIGHT || m == SDL_BUTTON_WHEELDOWN)
				{
					c = SDLK_PAGEDOWN;
				}
			}
		}
		else
		{
			UIObjectUnhighlight(sObjs);
		}
	}
	if (gEventHandlers.keyboard.modState & (KMOD_ALT | KMOD_CTRL))
	{
		switch (c)
		{
		case 'x':
			MissionTerminate(scrap);
			MissionCopy(scrap, CArrayGet(&gCampaign.Setting.Missions, *mission));
			Delete(*xc, *yc, mission);
			break;

		case 'c':
			MissionTerminate(scrap);
			MissionCopy(scrap, CArrayGet(&gCampaign.Setting.Missions, *mission));
			break;

		case 'v':
			InsertMission(*mission, scrap);
			fileChanged = 1;
			Setup(*mission, 0);
			break;

		case 'q':
			if (!fileChanged || ConfirmClose("Quit anyway? (Y/N)"))
			{
				*done = 1;
			}
			break;

		case 'n':
			InsertMission(gCampaign.Setting.Missions.size, NULL);
			*mission = gCampaign.Setting.Missions.size - 1;
			fileChanged = 1;
			Setup(*mission, 0);
			break;
				
		case 'o':
			if (!fileChanged || ConfirmClose("Open anyway? (Y/N)"))
			{
				Open();
				*mission = 0;
			}
			break;

		case 's':
			Save();
			break;

		case 'm':
			*willDisplayAutomap = 1;
			break;

		case 'e':
			EditCharacters(&gCampaign.Setting);
			Setup(*mission, 0);
			UIObjectUnhighlight(sObjs);
			break;
		}
	}
	else
	{
		switch (c)
		{
		case SDLK_F1:
			HelpScreen();
			break;

		case SDLK_HOME:
			if (*mission > 0)
			{
				(*mission)--;
			}
			Setup(*mission, 0);
			break;

		case SDLK_END:
			if (*mission < (int)gCampaign.Setting.Missions.size)
			{
				(*mission)++;
			}
			Setup(*mission, 0);
			break;

		case SDLK_INSERT:
			switch (*yc)
			{
			case YC_CHARACTERS:
				if (gCampaign.Setting.characters.OtherChars.size > 0)
				{
					int c = 0;
					CArrayPushBack(&currentMission->Enemies, &c);
					CharacterStoreAddBaddie(&gCampaign.Setting.characters, c);
					*xc = currentMission->Enemies.size - 1;
				}
				break;

			case YC_SPECIALS:
				if (gCampaign.Setting.characters.OtherChars.size > 0)
				{
					int c = 0;
					CArrayPushBack(&currentMission->SpecialChars, &c);
					CharacterStoreAddSpecial(&gCampaign.Setting.characters, c);
					*xc = currentMission->SpecialChars.size - 1;
				}
				break;

			case YC_ITEMS:
				{
					int item = 0;
					CArrayPushBack(&currentMission->Items, &item);
					CArrayPushBack(&currentMission->ItemDensities, &item);
					*xc = currentMission->Items.size - 1;
				}
				break;

			default:
				if (*yc >= YC_OBJECTIVES)
				{
					AddObjective(currentMission);
				}
				else
				{
					InsertMission(*mission, NULL);
				}
				break;
			}
			fileChanged = 1;
			Setup(*mission, 0);
			break;

		case SDLK_DELETE:
			Delete(*xc, *yc, mission);
			break;

		case SDLK_UP:
			MoveSelection(0, yc, xc);
			break;

		case SDLK_DOWN:
			MoveSelection(1, yc, xc);
			break;

		case SDLK_TAB:
			MoveSelection(
				!(gEventHandlers.keyboard.modState & KMOD_SHIFT), yc, xc);
			break;

		case SDLK_LEFT:
			(*xc)--;
			AdjustXC(*yc, xc);
			break;

		case SDLK_RIGHT:
			(*xc)++;
			AdjustXC(*yc, xc);
			break;

		case SDLK_PAGEUP:
			if (Change(o, *yc, 1, mission))
			{
				fileChanged = 1;
			}
			Setup(*mission, 0);
			break;

		case SDLK_PAGEDOWN:
			if (Change(o, *yc, -1, mission))
			{
				fileChanged = 1;
			}
			Setup(*mission, 0);
			break;

		case SDLK_ESCAPE:
			if (!fileChanged || ConfirmClose("Quit anyway? (Y/N)"))
			{
				*done = 1;
			}
			break;

		case SDLK_BACKSPACE:
			DelChar(*xc, *yc);
			fileChanged = 1;
			break;

		default:
			c = KeyGetTyped(&gEventHandlers.keyboard);
			if (c)
			{
				fileChanged = 1;
				AddChar(*xc, *yc, (char)c);
			}
			break;
		}
	}
}

static void EditCampaign(void)
{
	int done = 0;
	int mission = 0;
	int xc = 0, yc = 0;
	int xcOld, ycOld;
	Mission scrap;
	memset(&scrap, 0, sizeof scrap);

	gCampaign.seed = 0;
	Setup(mission, 1);

	SDL_EnableKeyRepeat(0, 0);
	while (!done)
	{
		int willDisplayAutomap = 0;
		int c, m;
		EventPoll(&gEventHandlers, SDL_GetTicks());
		c = KeyGetPressed(&gEventHandlers.keyboard);
		m = MouseGetPressed(&gEventHandlers.mouse);

		HandleInput(
			c, m,
			&xc, &yc, &xcOld, &ycOld,
			&mission, &scrap,
			&willDisplayAutomap, &done);
		Display(mission, yc, willDisplayAutomap);
		if (willDisplayAutomap)
		{
			GetKey(&gEventHandlers);
		}
		SDL_Delay(10);
	}
}


int main(int argc, char *argv[])
{
	int i;
	int loaded = 0;

	printf("C-Dogs SDL Editor\n");

	debug(D_NORMAL, "Initialising SDL...\n");
	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0)
	{
		printf("Failed to start SDL!\n");
		return -1;
	}
	SDL_EnableUNICODE(SDL_ENABLE);

	printf("Data directory:\t\t%s\n",	GetDataFilePath(""));
	printf("Config directory:\t%s\n\n",	GetConfigFilePath(""));

	if (!PicManagerTryInit(
		&gPicManager, "graphics/cdogs.px", "graphics/cdogs2.px"))
	{
		exit(0);
	}
	memcpy(origPalette, gPicManager.palette, sizeof origPalette);
	BuildTranslationTables(gPicManager.palette);
	CDogsTextInit(GetDataFilePath("graphics/font.px"), -2);

	// initialise UI collections
	// Note: must do this after text init since positions depend on text height
	sObjs = CreateMainObjs(&currentMission);

	CampaignInit(&gCampaign);

	ConfigLoadDefault(&gConfig);
	ConfigLoad(&gConfig, GetConfigFilePath(CONFIG_FILE));
	gConfig.Graphics.IsEditor = 1;
	BulletInitialize();
	WeaponInitialize();
	PlayerDataInitialize();
	GraphicsInit(&gGraphicsDevice);
	GraphicsInitialize(
		&gGraphicsDevice, &gConfig.Graphics, gPicManager.palette, 0);
	if (!gGraphicsDevice.IsInitialized)
	{
		printf("Video didn't init!\n");
		exit(EXIT_FAILURE);
	}

	// Reset campaign (graphics init may have created dummy campaigns)
	CampaignSettingTerminate(&gCampaign.Setting);
	CampaignSettingInit(&gCampaign.Setting);

	EventInit(&gEventHandlers, PicManagerGetOldPic(&gPicManager, 145));

	for (i = 1; i < argc; i++)
	{
		if (!loaded)
		{
			memset(lastFile, 0, sizeof(lastFile));
			strncpy(lastFile, argv[i], sizeof(lastFile) - 1);
			if (strchr(lastFile, '.') == NULL &&
				sizeof lastFile - strlen(lastFile) > 3)
			{
				strcat(lastFile, ".CPN");
			}
			if (LoadCampaignOld(lastFile, &gCampaign.Setting) == 0)
			{
				loaded = 1;
			}
		}
	}

	currentMission = NULL;

	EditCampaign();

	CampaignTerminate(&gCampaign);

	GraphicsTerminate(&gGraphicsDevice);
	PicManagerTerminate(&gPicManager);

	UIObjectDestroy(sObjs);

	exit(EXIT_SUCCESS);
}