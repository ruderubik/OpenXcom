/*
 * Copyright 2011 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "EndResearchState.h"
#include "../Engine/Game.h"
#include "../Engine/Palette.h"
#include "../Engine/Language.h"
#include "../Resource/ResourcePack.h"
#include "../Interface/TextButton.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Ruleset/RuleResearchProject.h"
#include "NewPossibleResearchState.h"
#include "../Ufopaedia/Ufopaedia.h"
#include <algorithm>
#include "../Savegame/SavedGame.h"

namespace OpenXcom
{
EndResearchState::EndResearchState(Game * game, Base * base, const RuleResearchProject * research) : State (game), _base(base), _research(research)
{
	_screen = false;

	// Create objects
	_window = new Window(this, 210, 140, 70, 30, POPUP_BOTH);
	_btnOk = new TextButton(80, 16, 80, 145);
	_btnReport = new TextButton(80, 16, 190, 145);
	_txtTitle = new Text(200, 16, 80, 90);

	// Set palette
	_game->setPalette(_game->getResourcePack()->getPalette("BACKPALS.DAT")->getColors(Palette::blockOffset(6)), Palette::backPos, 16);

	add(_window);
	add(_btnOk);
	add(_btnReport);
	add(_txtTitle);

	// Set up objects
	_window->setColor(Palette::blockOffset(8)+8);
	_window->setBackground(_game->getResourcePack()->getSurface("BACK05.SCR"));

	_btnOk->setColor(Palette::blockOffset(8)+8);
	_btnOk->setText(_game->getLanguage()->getString("STR_OK"));
	_btnOk->onMouseClick((ActionHandler)&EndResearchState::btnOkClick);
	_btnReport->setColor(Palette::blockOffset(8)+8);
	_btnReport->setText(_game->getLanguage()->getString("STR_VIEW_REPORTS"));
	_btnReport->onMouseClick((ActionHandler)&EndResearchState::btnReportClick);

	_txtTitle->setColor(Palette::blockOffset(8)+5);
	_txtTitle->setBig();
	_txtTitle->setAlign(ALIGN_CENTER);
	_txtTitle->setText(_game->getLanguage()->getString("STR_RESEARCH_COMPLETED"));
}

void EndResearchState::btnOkClick(Action *action)
{
	std::vector<RuleResearchProject *> newPossibleResearch;
	_game->getSavedGame()->getDependableResearch (newPossibleResearch, _research, _game->getRuleset(), _base);
	_game->pushState (new NewPossibleResearchState(_game, _base, newPossibleResearch));
}

void EndResearchState::btnReportClick(Action *action)
{
	std::string name (_research->getName ());
	Ufopaedia::openArticle(_game, name);
}

}
