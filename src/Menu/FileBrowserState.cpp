/*
 * Copyright 2010-2021 OpenXcom Developers.
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
#include "FileBrowserState.h"
#include <sstream>
#include "../version.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/Screen.h"
#include "../Engine/Action.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Logger.h"
#include "../Engine/Screen.h"
#include "../Engine/CrossPlatform.h"
#include "../Engine/InteractiveSurface.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/ArrowButton.h"
#include "../Interface/TextButton.h"
#include "../Interface/TextEdit.h"
#include "../Interface/TextList.h"
#include "../Interface/Frame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"

namespace OpenXcom
{

struct FileSorter
{
	bool _foldersFirst;
	bool _sortByName;

	FileSorter(bool foldersFirst, bool sortByName) : _foldersFirst(foldersFirst), _sortByName(sortByName) {}

	bool operator()(const FileData& fileA, const FileData& fileB) const
	{
		if (fileA.isFolder && !fileB.isFolder)
		{
			return _foldersFirst ? true : false;
		}
		else if (!fileA.isFolder && fileB.isFolder)
		{
			return _foldersFirst ? false : true;
		}

		if (_sortByName)
		{
			return Unicode::naturalCompare(fileA.name, fileB.name);
		}
		else
		{
			return fileA.timestamp < fileB.timestamp;
		}
	}

};

/**
 * Initializes all the elements in the File Browser window.
 * @param game Pointer to the core game.
 */
FileBrowserState::FileBrowserState(State *parent, bool saveMode, std::string fileName) : _parent(parent), _saveMode(saveMode)
{
	if (_game->getSavedGame() && _game->getSavedGame()->getSavedBattle() && Options::maximizeInfoScreens)
	{
		Options::baseXResolution = Screen::ORIGINAL_WIDTH;
		Options::baseYResolution = Screen::ORIGINAL_HEIGHT;
		_game->getScreen()->resetDisplay(false);
        _window = new Window(this, 320, 200, 0, 0, POPUP_NONE);
	}
    else
    {
	    _window = new Window(this, 320, 200, 0, 0, POPUP_BOTH);
    }

	int fileNameWidth = 148;
	int fileTypeWidth = 40;
	int fileDateWidth = 60;
	int fileTimeWidth = 40;

	_txtTitle = new Text(320, 17, 0, 9);
	_txtDirectory = new Text(297, 10, 14, 26);
	
	_txtFilename = new Text(fileNameWidth, 10, 14, 36);
	_txtFiletype = new Text(fileTypeWidth, 10, 14 + fileNameWidth, 36);
	_txtFiledate = new Text(fileDateWidth, 10, 14 + fileNameWidth + fileTypeWidth, 36);

	_sortName = new ArrowButton(ARROW_SMALL_DOWN, 11, 8, 14, 36);
	_sortType = new ArrowButton(ARROW_SMALL_DOWN, 11, 8, 14 + fileNameWidth, 36);
	_sortDate = new ArrowButton(ARROW_NONE, 11, 8, 14 + fileNameWidth + fileTypeWidth, 36);

	_btnSelect = new TextButton(64, 16, 0, 0);
	_btnCut = new TextButton(64, 16, 0, 0);
	_btnCopy = new TextButton(64, 16, 0, 0);
	_btnPaste = new TextButton(64, 16, 0, 0);
	_btnClose = new TextButton(64, 16, 0, 0);

	_rightClickMenu.push_back(_btnSelect);
	_rightClickMenu.push_back(_btnCut);
	_rightClickMenu.push_back(_btnCopy);
	_rightClickMenu.push_back(_btnPaste);
	_rightClickMenu.push_back(_btnClose);

	_btnOk = new TextButton(100, 16, 8, 176);
	_btnCancel = new TextButton(100, 16, 110, 176);

	_lstBrowser = new TextList(274, 104, 14, 52);

    _frameBrowser = new Frame(304, 116, 8, 46);

    _txtSearch = new Text(48, 10, 8, 164);
    _edtQuickSearch = new TextEdit(this, 254, 10, 58, 164);

	// Set palette
	setInterface("mainMenu");

	add(_window, "window", "mainMenu");
	add(_txtTitle, "text", "mainMenu");
	add(_txtDirectory, "text", "mainMenu");
	add(_txtFilename, "text", "mainMenu");
	add(_txtFiletype, "text", "mainMenu");
	add(_txtFiledate, "text", "mainMenu");
	add(_sortName, "button", "mainMenu");
	add(_sortType, "button", "mainMenu");
	add(_sortDate, "button", "mainMenu");
	add(_btnOk, "button", "mainMenu");
	add(_btnCancel, "button", "mainMenu");
    add(_lstBrowser, "list", "saveMenus");
    add(_frameBrowser, "frames", "newBattleMenu");
	add(_txtSearch, "text", "mainMenu");
	add(_edtQuickSearch, "button", "mainMenu");
	for (auto button : _rightClickMenu)
	{
		add(button, "button", "mainMenu");
	}

	centerAllSurfaces();

	// Set up objects
	setWindowBackground(_window, "mainMenu");

	_txtTitle->setAlign(ALIGN_CENTER);
	_txtTitle->setBig();
	_txtTitle->setText(tr(saveMode? "STR_FILE_BROWSER_SAVE_TITLE" : "STR_FILE_BROWSER_OPEN_TITLE"));

	_txtFilename->setText(tr("STR_FILE_BROWSER_FILENAME"));
	_txtFiletype->setText(tr("STR_FILE_BROWSER_FILETYPE"));
	_txtFiledate->setText(tr("STR_FILE_BROWSER_FILEDATE"));

	_sortName->setX(_sortName->getX() + _txtFilename->getTextWidth() + 5);
	_sortName->onMouseClick((ActionHandler)&FileBrowserState::sortArrowClick);

	_sortType->setX(_sortType->getX() + _txtFiletype->getTextWidth() + 5);
	_sortType->onMouseClick((ActionHandler)&FileBrowserState::sortArrowClick);

	_sortDate->setX(_sortDate->getX() + _txtFiledate->getTextWidth() + 5);
	_sortDate->onMouseClick((ActionHandler)&FileBrowserState::sortArrowClick);

	_btnSelect->setText(tr("STR_FILE_BROWSER_SELECT"));
	_btnSelect->onMousePress((ActionHandler)&FileBrowserState::btnSelectClick);

	_btnCut->setText(tr("STR_FILE_BROWSER_CUT"));
	_btnCut->onMousePress((ActionHandler)&FileBrowserState::btnCutClick);

	_btnCopy->setText(tr("STR_FILE_BROWSER_COPY"));
	_btnCopy->onMousePress((ActionHandler)&FileBrowserState::btnCopyClick);

	_btnPaste->setText(tr("STR_FILE_BROWSER_PASTE"));
	_btnPaste->onMousePress((ActionHandler)&FileBrowserState::btnPasteClick);

	_btnClose->setText(tr("STR_FILE_BROWSER_CLOSE"));
	_btnClose->onMousePress((ActionHandler)&FileBrowserState::btnCloseClick);

	for (auto button : _rightClickMenu)
	{
		button->setVisible(false);
	}

	_btnOk->setText(tr(saveMode? "STR_FILE_BROWSER_SAVE" : "STR_FILE_BROWSER_OPEN"));
	_btnOk->onMouseClick((ActionHandler)&FileBrowserState::btnOkClick);
	_btnOk->onKeyboardPress((ActionHandler)&FileBrowserState::btnOkClick, Options::keyOk);

	_btnCancel->setText(tr("STR_CANCEL"));
	_btnCancel->onMouseClick((ActionHandler)&FileBrowserState::btnCancelClick);
	_btnCancel->onKeyboardPress((ActionHandler)&FileBrowserState::btnCancelClick, Options::keyCancel);
    _btnCancel->onKeyboardRelease((ActionHandler)&FileBrowserState::edtQuickSearchFocus, Options::keyToggleQuickSearch);

	_txtSearch->setText(tr(saveMode ? "STR_FILE_BROWSER_ENTER_NAME" : "STR_FILE_BROWSER_SEARCH"));

	_edtQuickSearch->setX(_txtSearch->getX() + _txtSearch->getTextWidth() + 5);
	_edtQuickSearch->setWidth(304 - _txtSearch->getTextWidth() - 5);
    _edtQuickSearch->setText(fileName);
    _edtQuickSearch->setColor(15 * 16 - 1);
    _edtQuickSearch->onEnter((ActionHandler)&FileBrowserState::edtQuickSearchApply);

	// add date modified and other sort stuff?
	// plus sorts for name/modified
    _lstBrowser->setColumns(4, fileNameWidth, fileTypeWidth, fileDateWidth, fileTimeWidth);
    _lstBrowser->setAlign(ALIGN_LEFT);
    _lstBrowser->setSelectable(true);
	_lstBrowser->setBackground(_window);
    _lstBrowser->onMouseClick((ActionHandler)&FileBrowserState::lstBrowserClick, SDL_BUTTON_LEFT);
    _lstBrowser->onMouseClick((ActionHandler)&FileBrowserState::rightClickMenuOpen, SDL_BUTTON_RIGHT);
}

FileBrowserState::~FileBrowserState()
{
	
}

void FileBrowserState::init()
{
	State::init();

	_foldersFirst = true;
	_sortByName = true;
	_reverseSort = false;
    populateBrowserList("");

	_mouseOverRightClickMenu = false;
}

/**
 * Populates the list of files and folders in the browser
 * @param directory the file directory from which to list contents
 */
void FileBrowserState::populateBrowserList(std::string directory)
{
	_clickedRow = -1;

	std::string searchString = _edtQuickSearch->getText();
	Unicode::upperCase(searchString);

    _lstBrowser->clearList();

	if (directory.empty())
	{
		directory = Options::getUserFolder(); //getMasterUserFolder()
	}

	_currentDirectory = directory;

	_txtDirectory->setText(tr("STR_FILE_BROWSER_CD").arg(_currentDirectory));

	_lstBrowser->addRow(1, tr("STR_USER_FOLDER").c_str());
	_lstBrowser->addRow(1, tr("STR_FILE_BROWSER_UP_ONE_DIRECTORY").c_str());

	// change "" to exention to search
	auto directoryContents = CrossPlatform::getFolderContents(directory, "");
	_fileData.clear();
	std::copy(directoryContents.begin(), directoryContents.end(), std::back_inserter(_fileData));
	if (_reverseSort)
	{
		std::sort(_fileData.rbegin(), _fileData.rend(), FileSorter(_foldersFirst, _sortByName));
	}
	else
	{
		std::sort(_fileData.begin(), _fileData.end(), FileSorter(_foldersFirst, _sortByName));
	}

	// now list the files/folders
	for (auto file : _fileData)
	{
		auto fileName = file.name;
		if (!searchString.empty())
		{
			std::string uppercaseName = fileName;
			Unicode::upperCase(uppercaseName);
			if (uppercaseName.find(searchString) == std::string::npos)
			{
				continue;
			}
		}

		std::string fileType = file.isFolder ? tr("STR_FILE_BROWSER_DIR") : tr("STR_FILE_BROWSER_FIL");
		std::pair<std::string, std::string> fileTime = CrossPlatform::timeToString(file.timestamp);

		_lstBrowser->addRow(4, fileName.c_str(), fileType.c_str(), fileTime.first.c_str(), fileTime.second.c_str());
	}
}

/**
 * Handles clicking the sort arrows
 * @param action Pointer to an action.
 */
void FileBrowserState::sortArrowClick(Action *action)
{
	if (action->getSender() == _sortName)
	{
		if (_sortByName)
		{
			_foldersFirst = !_foldersFirst;
			_reverseSort = !_reverseSort;
		}
		else
		{
			_sortByName = true;
			if (_reverseSort)
			{
				_foldersFirst = !_foldersFirst;
			}
			_reverseSort = false;
		}

		_sortName->setShape(_reverseSort ? ARROW_SMALL_UP : ARROW_SMALL_DOWN);
		_sortDate->setShape(ARROW_NONE);
	}
	else if (action->getSender() == _sortType)
	{
		_foldersFirst = !_foldersFirst;
		_sortType->setShape(!_foldersFirst ? ARROW_SMALL_UP : ARROW_SMALL_DOWN);
	}
	else
	{
		// if not sorting by name, then we're sorting by date
		if (!_sortByName)
		{
			_foldersFirst = !_foldersFirst;
			_reverseSort = !_reverseSort;
		}
		else
		{
			_sortByName = false;
			if (_reverseSort)
			{
				_foldersFirst = !_foldersFirst;
			}
			_reverseSort = false;
		}

		_sortDate->setShape(_reverseSort ? ARROW_SMALL_UP : ARROW_SMALL_DOWN);
		_sortName->setShape(ARROW_NONE);
	}

	populateBrowserList(_currentDirectory);
}

/**
 * Handles focusing the quick search filter
 * @param action Pointer to an action.
 */
void FileBrowserState::edtQuickSearchFocus(Action *action)
{
    if (!_edtQuickSearch->isFocused())
    {
        _edtQuickSearch->setText("");
        _edtQuickSearch->setFocus(true);
        edtQuickSearchApply(0);
    }
}

/**
 * Handles applying the quick search filter
 * @param action Pointer to an action.
 */
void FileBrowserState::edtQuickSearchApply(Action *action)
{
    populateBrowserList(_currentDirectory);
}

/**
 * Handles clicking on the list of the browser window
 * @param action Pointer to an action.
 */
void FileBrowserState::lstBrowserClick(Action *action)
{
	if (_btnSelect->getVisible())
	{
		btnCloseClick(action);
	}

	size_t selected = _lstBrowser->getSelectedRow();

	if ((int)selected != _clickedRow)
	{
		if (_clickedRow > -1)
		{
			_lstBrowser->setRowColor(_clickedRow, _lstBrowser->getColor());
		}
		_lstBrowser->setRowColor(selected, 244); // change to options for secondary color
		_clickedRow = selected;

		_firstClickTime = SDL_GetTicks();
		return;
	}
	else if ((int)(SDL_GetTicks() - _firstClickTime) > (Options::dragScrollTimeTolerance))
	{
		_firstClickTime = SDL_GetTicks();
		return;
	}
	
	switch (selected)
	{
		// return to the main user folder
		case 0 :
			{
				populateBrowserList("");
			}

			break;

		// go up one level
		case 1 :
			{
				_currentDirectory.erase(_currentDirectory.size() - 1, std::string::npos);
				std::size_t lastBackslash = _currentDirectory.find_last_of("/");
				_currentDirectory.erase(lastBackslash + 1, std::string::npos);
				populateBrowserList(_currentDirectory);
			}

			break;

		// selecting a file/folder
		default :
			{
				selected -= 2;
				FileData clickedFile = _fileData.at(selected);

				if (clickedFile.isFolder)
				{
					_currentDirectory = _currentDirectory + clickedFile.name + "/";
					populateBrowserList(_currentDirectory);
				}
			}

			break;

	}
}

/**
 * Handles right-clicking to bring up a menu of actions
 * @param action Pointer to an action.
 */
void FileBrowserState::rightClickMenuOpen(Action *action)
{
	size_t selected = _lstBrowser->getSelectedRow();

	if ((int)selected != _clickedRow)
	{
		if (_clickedRow > -1)
		{
			_lstBrowser->setRowColor(_clickedRow, _lstBrowser->getColor());
		}
		_lstBrowser->setRowColor(selected, 244); // change to options for secondary color
		_clickedRow = selected;
	}

	int mouseY = action->getAbsoluteYMouse();
	int mouseX = action->getAbsoluteXMouse();
	bool openUp = mouseY > (200 - 5 * _btnSelect->getHeight());
	bool openLeft = mouseX > (320 - _btnSelect->getWidth());

	int btnIndex = 0;
	for (auto button : _rightClickMenu)
	{
		button->setY(openUp ? mouseY - (btnIndex + 1) * button->getHeight() : mouseY + btnIndex * button->getHeight());
		button->setX(openLeft ? mouseX - button->getWidth() : mouseX);
		button->setVisible(true);
		++btnIndex;
	}
}

/**
 * Handles clicking the select button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnSelectClick(Action *action)
{
	btnCloseClick(action);
}

/**
 * Handles clicking the cut button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnCutClick(Action *action)
{
	btnCloseClick(action);
}

/**
 * Handles clicking the copy button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnCopyClick(Action *action)
{
	btnCloseClick(action);
}

/**
 * Handles clicking the paste button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnPasteClick(Action *action)
{
	btnCloseClick(action);
}

/**
 * Handles clicking the close button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnCloseClick(Action *action)
{
	for (auto button : _rightClickMenu)
	{
		button->setVisible(false);
	}

	// consume the action so nothing underneath gets clicked
	action->getDetails()->type = SDL_NOEVENT;
}

/**
 * Handles clicking the OK button
 * @param action Pointer to an action.
 */
void FileBrowserState::btnOkClick(Action *)
{
	_parent->setFileName("");
    _game->popState();
}

/**
 * Returns to the main menu.
 * @param action Pointer to an action.
 */
void FileBrowserState::btnCancelClick(Action *)
{
	_game->popState();
}

}