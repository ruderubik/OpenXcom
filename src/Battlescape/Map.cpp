/*
 * Copyright 2010-2016 OpenXcom Developers.
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
#include <math.h>
#include "Map.h"
#include "Camera.h"
#include "UnitSprite.h"
#include "ItemSprite.h"
#include "Pathfinding.h"
#include "TileEngine.h"
#include "Projectile.h"
#include "Explosion.h"
#include "BattlescapeState.h"
#include "MapEditor.h"
#include "MapEditorState.h"
#include "Particle.h"
#include "../Mod/Mod.h"
#include "../Engine/Action.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Timer.h"
#include "../Engine/Language.h"
#include "../Engine/Palette.h"
#include "../Engine/Game.h"
#include "../Engine/Screen.h"
#include "../Engine/ShaderDraw.h"
#include "../Engine/ShaderMove.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/Node.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Ufopaedia/Ufopaedia.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/MapData.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleEnviroEffects.h"
#include "BattlescapeMessage.h"
#include "../Savegame/SavedGame.h"
#include "../Interface/NumberText.h"
#include "../Interface/Text.h"
#include "../fmath.h"


/*
  1) Map origin is top corner.
  2) X axis goes downright. (width of the map)
  3) Y axis goes downleft. (length of the map
  4) Z axis goes up (height of the map)

           0,0
            /\
           /  \
        y+ \  / x+
            \/

  Compass directions

         W  /\  N
           /  \
           \  /
         S  \/  E

  Unit directions

         6  /\  0
           /  \
           \  /
         4  \/  2

  Big units parts

            /\
           /0 \
          /\  /\
         /2 \/1 \
         \  /\  /
          \/3 \/
           \  /
            \/
 */

namespace OpenXcom
{

/**
 * Sets up a map with the specified size and position.
 * @param game Pointer to the core game.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 * @param visibleMapHeight Current visible map height.
 */
Map::Map(Game *game, int width, int height, int x, int y, int visibleMapHeight, bool keepObstacleTimerRunning) : InteractiveSurface(width, height, x, y),
	_game(game), _arrow(0), _anyIndicator(false), _isAltPressed(false),
	_selectorX(0), _selectorY(0), _mouseX(0), _mouseY(0), _cursorType(CT_NORMAL), _cursorSize(1), _animFrame(0),
	_projectile(0), _followProjectile(true), _projectileInFOV(false), _explosionInFOV(false), _launch(false), _visibleMapHeight(visibleMapHeight),
	_unitDying(false), _smoothingEngaged(false), _flashScreen(false), _bgColor(15), _projectileSet(0), _showObstacles(false)
{
	_iconHeight = _game->getMod()->getInterface("battlescape")->getElement("icons")->h;
	_iconWidth = _game->getMod()->getInterface("battlescape")->getElement("icons")->w;
	_messageColor = _game->getMod()->getInterface("battlescape")->getElement("messageWindows")->color;

	PathPreview previewSetting = Options::battleNewPreviewPath;
	_smoothCamera = Options::battleSmoothCamera;
	if (Options::traceAI)
	{
		// turn everything on because we want to see the markers.
		previewSetting = PATH_ARROW_TU;
	}
	_previewSettingArrows = previewSetting & PATH_ARROWS;
	_previewSettingTu     = previewSetting & PATH_TU_COST;
	_previewSettingEnergy = previewSetting & PATH_ENERGY_COST;

	_save = _game->getSavedGame()->getSavedBattle();
	if ((int)(_game->getMod()->getLUTs()->size()) > _save->getDepth())
	{
		_transparencies = &_game->getMod()->getLUTs()->at(_save->getDepth());
	}
	else
	{
		const static std::vector<Uint8> dummy;
		_transparencies = &dummy;
	}

	_spriteWidth = _game->getMod()->getSurfaceSet("BLANKS.PCK")->getFrame(0)->getWidth();
	_spriteHeight = _game->getMod()->getSurfaceSet("BLANKS.PCK")->getFrame(0)->getHeight();
	_message = new BattlescapeMessage(320, (visibleMapHeight < 200)? visibleMapHeight : 200, 0, 0);
	_message->setX(_game->getScreen()->getDX());
	_message->setY((visibleMapHeight - _message->getHeight()) / 2);
	_message->setTextColor(_messageColor);
	_camera = new Camera(_spriteWidth, _spriteHeight, _save->getMapSizeX(), _save->getMapSizeY(), _save->getMapSizeZ(), this, visibleMapHeight);
	_scrollMouseTimer = new Timer(SCROLL_INTERVAL);
	_scrollMouseTimer->onTimer((SurfaceHandler)&Map::scrollMouse);
	_scrollKeyTimer = new Timer(SCROLL_INTERVAL);
	_scrollKeyTimer->onTimer((SurfaceHandler)&Map::scrollKey);
	_camera->setScrollTimer(_scrollMouseTimer, _scrollKeyTimer);
	_obstacleTimer = new Timer(2500);
	_obstacleTimer->stop();
	if (keepObstacleTimerRunning)
	{
		_obstacleTimer->onTimer((SurfaceHandler)&Map::enableObstacles);
	}
	else
	{
		_obstacleTimer->onTimer((SurfaceHandler)&Map::disableObstacles);
	}

	_txtAccuracy = new Text(44, 18, 0, 0);
	_txtAccuracy->setSmall();
	_txtAccuracy->setPalette(_game->getScreen()->getPalette());
	_txtAccuracy->setHighContrast(true);
	_txtAccuracy->initText(_game->getMod()->getFont("FONT_BIG"), _game->getMod()->getFont("FONT_SMALL"), _game->getLanguage());
	_cacheActiveWeaponUfopediaArticleUnlocked = -1;
	_cacheIsCtrlPressed = false;
	_cacheCursorPosition = TileEngine::invalid;
	_cacheHasLOS = -1;
	_cacheAccuracy = -1;

	_nightVisionOn = false;
	if (Options::oxceToggleNightVisionType == 2)
	{
		// persisted per campaign
		_nightVisionOn = _game->getSavedGame()->getToggleNightVision();
	}
	else if (Options::oxceToggleNightVisionType == 1)
	{
		// persisted per battle
		_nightVisionOn = _save->getToggleNightVision();
	}

	_debugVisionMode = 0;
	if (Options::oxceToggleBrightnessType == 2)
	{
		// persisted per campaign
		_debugVisionMode = _game->getSavedGame()->getToggleBrightness();
	}
	else if (Options::oxceToggleBrightnessType == 1)
	{
		// persisted per battle
		_debugVisionMode = _save->getToggleBrightness();
	}

	_fadeShade = 16;
	_nvColor = 0;
	_fadeTimer = new Timer(FADE_INTERVAL);
	_fadeTimer->onTimer((SurfaceHandler)&Map::fadeShade);
	_fadeTimer->start();

	auto enviro = _save->getEnviroEffects();
	if (enviro)
	{
		_bgColor = enviro->getMapBackgroundColor();
	}

	_stunIndicator = _game->getMod()->getSurface("FloorStunIndicator", false);
	_woundIndicator = _game->getMod()->getSurface("FloorWoundIndicator", false);
	_burnIndicator = _game->getMod()->getSurface("FloorBurnIndicator", false);
	_shockIndicator = _game->getMod()->getSurface("FloorShockIndicator", false);
	_anyIndicator = _stunIndicator || _woundIndicator || _burnIndicator || _shockIndicator;

	if (enviro)
	{
		if (!enviro->getMapShockIndicator().empty())
		{
			_shockIndicator = _game->getMod()->getSurface(enviro->getMapShockIndicator(), false);
		}
	}

	_vaporParticlesInit.resize(_camera->getMapSizeY() * _camera->getMapSizeX());
	_vaporParticles.resize(_camera->getMapSizeY() * _camera->getMapSizeX());
}

/**
 * Deletes the map.
 */
Map::~Map()
{
	delete _scrollMouseTimer;
	delete _scrollKeyTimer;
	delete _fadeTimer;
	delete _obstacleTimer;
	delete _arrow;
	delete _message;
	delete _camera;
	delete _txtAccuracy;
}

/**
 * Initializes the map.
 */
void Map::init()
{
	// load the tiny arrow into a surface
	int f = Palette::blockOffset(1); // yellow
	int b = 15; // black
	int pixels[81] = { 0, 0, b, b, b, b, b, 0, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   b, b, b, f, f, f, b, b, b,
					   b, f, f, f, f, f, f, f, b,
					   0, b, f, f, f, f, f, b, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   0, 0, 0, b, f, b, 0, 0, 0,
					   0, 0, 0, 0, b, 0, 0, 0, 0 };

	_arrow = new Surface(9, 9);
	_arrow->setPalette(this->getPalette());
	_arrow->lock();
	for (int y = 0; y < 9;++y)
		for (int x = 0; x < 9; ++x)
			_arrow->setPixel(x, y, pixels[x+(y*9)]);
	_arrow->unlock();

	_projectile = 0;
	if (_save->getDepth() == 0)
	{
		_projectileSet = _game->getMod()->getSurfaceSet("Projectiles");
	}
	else
	{
		_projectileSet = _game->getMod()->getSurfaceSet("UnderwaterProjectiles");
	}
}

/**
 * Keeps the animation timers running.
 */
void Map::think()
{
	_scrollMouseTimer->think(0, this);
	_scrollKeyTimer->think(0, this);
	_fadeTimer->think(0, this);
	_obstacleTimer->think(0, this);
}

/**
 * Draws the whole map, part by part.
 */
void Map::draw()
{
	if (!_redraw)
	{
		return;
	}

	// normally we'd call for a Surface::draw();
	// but we don't want to clear the background with colour 0, which is transparent (aka black)
	// we use colour 15 because that actually corresponds to the colour we DO want in all variations of the xcom and tftd palettes.
	// Note: un-hardcoded the color from 15 to ruleset value, default 15
	_redraw = false;
	ShaderDrawFunc(
		[](Uint8& dest, Uint8 color)
		{
			dest = color;
		},
		ShaderSurface(this),
		ShaderScalar<Uint8>(Palette::blockOffset(0) + _bgColor)
	);

	Tile *t;

	_projectileInFOV = _save->getDebugMode();
	if (_projectile)
	{
		t = _save->getTile(_projectile->getPosition(0).toTile());
		if (_save->getSide() == FACTION_PLAYER || (t && t->getVisible()))
		{
			_projectileInFOV = true;
		}
	}
	_explosionInFOV = _save->getDebugMode();
	if (!_explosions.empty())
	{
		for (auto* explosion : _explosions)
		{
			t = _save->getTile(explosion->getPosition().toTile());
			if (t && (explosion->isBig() || t->getVisible()))
			{
				_explosionInFOV = true;
				break;
			}
		}
	}

	if ((_save->getSelectedUnit() && _save->getSelectedUnit()->getVisible()) || _unitDying || _save->getSide() == FACTION_PLAYER || _save->getDebugMode() || _projectileInFOV || _explosionInFOV)
	{
		drawTerrain(this);
	}
	else
	{
		_message->blit(this->getSurface());
	}
}

/**
 * Replaces a certain amount of colors in the surface's palette.
 * @param colors Pointer to the set of colors.
 * @param firstcolor Offset of the first color to replace.
 * @param ncolors Amount of colors to replace.
 */
void Map::setPalette(const SDL_Color *colors, int firstcolor, int ncolors)
{
	Surface::setPalette(colors, firstcolor, ncolors);
	for (auto* mds : *_save->getMapDataSets())
	{
		mds->getSurfaceset()->setPalette(colors, firstcolor, ncolors);
	}
	_message->setPalette(colors, firstcolor, ncolors);
	refreshHiddenMovementBackground();
	_message->initText(_game->getMod()->getFont("FONT_BIG"), _game->getMod()->getFont("FONT_SMALL"), _game->getLanguage());
	_message->setText(_game->getLanguage()->getString("STR_HIDDEN_MOVEMENT"));
}

void Map::refreshHiddenMovementBackground()
{
	_message->setBackground(_game->getMod()->getSurface(_save->getHiddenMovementBackground()));
}

/**
 * Get shade of wall.
 * @param part For what wall do calculations.
 * @param tileFrot Tile of wall.
 * @return Current shade of wall.
 */
int Map::getWallShade(TilePart part, Tile* tileFrot)
{
	int shade;
	if (tileFrot->isDiscovered(O_FLOOR))
	{
		shade = reShade(tileFrot);
	}
	else
	{
		shade = 16;
	}
	if (part)
	{
		if ((tileFrot->isDoor(part) || tileFrot->isUfoDoor(part)) && tileFrot->isDiscovered(part))
		{
			Position offset =
				part == O_NORTHWALL ? Position(1,0,0) :
				part == O_WESTWALL ? Position(0,1,0) :
					throw Exception("Unsupported tile part for wall shade");

			Tile *tileBehind = _save->getTile(tileFrot->getPosition() - offset);

			shade = std::min(reShade(tileFrot), tileBehind ? tileBehind->getShade() + 5 : 16);
		}
	}
	return shade;
}

/**
 * Check two positions if have same XY cords
 */
static bool positionHaveSameXY(Position a, Position b)
{
	return a.x == b.x && a.y == b.y;
}

/**
 * Check two positions if have same XY cords
 */
static bool positionInRangeXY(Position a, Position b, int diff)
{
	return std::abs(a.x - b.x) <= diff && std::abs(a.y - b.y) <= diff;
}

namespace
{
static const std::vector<std::string> shootingRelativeOriginsDesc = {"Center view", "Left shift", "Right shift"};
static const int ArrowBobOffsets[8] = {0,1,2,1,0,1,2,1};

int getArrowBobForFrame(int frame)
{
	return ArrowBobOffsets[frame % 8];
}

int getShadePulseForFrame(int shade, int frame)
{
	if (shade > 7) shade = 7;
	if (shade < 2) shade = 2;
	shade += (ArrowBobOffsets[frame % 8] * 2 - 2);
	return shade;
}

}

/**
 * Draw part of unit graphic that overlap current tile.
 * @param surface
 * @param unitTile
 * @param currTile
 * @param currTileScreenPosition
 * @param shade
 * @param obstacleShade
 * @param topLayer
 */
void Map::drawUnit(UnitSprite &unitSprite, Tile *unitTile, Tile *currTile, Position currTileScreenPosition, bool topLayer, BattleUnit* movingUnit)
{
	const int tileFoorWidth = 32;
	const int tileFoorHeight = 16;
	const int tileHeight = 40;

	if (!unitTile)
	{
		return;
	}
	BattleUnit* bu = unitTile->getOverlappingUnit(_save, TUO_ALWAYS);
	Position unitOffset;
	bool unitFromBelow = false;
	bool unitFromAbove = false;
	if (bu)
	{
		if (bu != unitTile->getUnit())
		{
			unitFromBelow = true;
		}
	}
	else if (movingUnit && unitTile == currTile)
	{
		auto upperTile = _save->getAboveTile(unitTile);
		if (upperTile && upperTile->hasNoFloor(_save))
		{
			bu = upperTile->getUnit();
		}
		if (bu != movingUnit)
		{
			return;
		}
		unitFromAbove = true;
	}
	else
	{
		return;
	}

	if (!(bu->getVisible() || _save->getDebugMode()))
	{
		return;
	}

	unitOffset.x = unitTile->getPosition().x - bu->getPosition().x;
	unitOffset.y = unitTile->getPosition().y - bu->getPosition().y;
	int part = unitOffset.x + unitOffset.y*2;

	bool moving = bu->getStatus() == STATUS_WALKING || bu->getStatus() == STATUS_FLYING;
	int bonusWidth = moving ? 0 : tileFoorWidth;
	int topMargin = 0;
	int bottomMargin = 0;

	//if unit is from below then we draw only part that in in tile
	if (unitFromBelow)
	{
		bottomMargin = -tileFoorHeight / 2;
		topMargin = tileFoorHeight;
	}
	else if (topLayer)
	{
		topMargin = 2 * tileFoorHeight;
	}
	else
	{
		const Tile *top = _save->getAboveTile(unitTile);
		if (top && top->getOverlappingUnit(_save, TUO_ALWAYS) == bu)
		{
			topMargin = -tileFoorHeight / 2;
		}
		else
		{
			topMargin = tileFoorHeight;
		}
	}

	GraphSubset mask = GraphSubset(tileFoorWidth + bonusWidth, tileHeight + topMargin + bottomMargin).offset(currTileScreenPosition.x - bonusWidth / 2, currTileScreenPosition.y - topMargin);

	if (moving)
	{
		GraphSubset leftMask = mask.offset(-tileFoorWidth/2, 0);
		GraphSubset rightMask = mask.offset(+tileFoorWidth/2, 0);
		int direction = bu->getDirection();
		Position partCurr = currTile->getPosition();
		Position partDest = bu->getDestination() + unitOffset;
		Position partLast = bu->getLastPosition() + unitOffset;
		bool isTileDestPos = positionHaveSameXY(partDest, partCurr);
		bool isTileLastPos = positionHaveSameXY(partLast, partCurr);

		if (unitFromAbove && partLast != unitTile->getPosition())
		{
			//this tile is below moving unit and it do not change levels, nothing to draw
			return;
		}

		//adjusting mask
		if (positionHaveSameXY(partLast, partDest))
		{
			if (currTile == unitTile)
			{
				//no change
			}
			else
			{
				//nothing to draw
				return;
			}
		}
		else if (isTileDestPos)
		{
			//unit is moving to this tile
			switch (direction)
			{
			case 0:
			case 1:
				mask = GraphSubset::intersection(mask, rightMask);
				break;
			case 2:
				//no change
				break;
			case 3:
				//no change
				break;
			case 4:
				//no change
				break;
			case 5:
			case 6:
				mask = GraphSubset::intersection(mask, leftMask);
				break;
			case 7:
				//nothing to draw
				return;
			}
		}
		else if (isTileLastPos)
		{
			//unit is exiting this tile
			switch (direction)
			{
			case 0:
				//no change
				break;
			case 1:
			case 2:
				mask = GraphSubset::intersection(mask, leftMask);
				break;
			case 3:
				//nothing to draw
				return;
			case 4:
			case 5:
				mask = GraphSubset::intersection(mask, rightMask);
				break;
			case 6:
				//no change
				break;
			case 7:
				//no change
				break;
			}
		}
		else
		{
			Position leftPos = partCurr + Position(-1, 0, 0);
			Position rightPos = partCurr + Position(0, -1, 0);
			if (!topLayer && (partDest.z > partCurr.z || partLast.z > partCurr.z))
			{
				//unit change layers, it will be drawn by upper layer not lower.
				return;
			}
			else if (
				(direction == 1 && (partDest == rightPos || partLast == leftPos)) ||
				(direction == 5 && (partDest == leftPos || partLast == rightPos)))
			{
				mask = GraphSubset(tileFoorWidth, tileHeight + 2 * tileFoorHeight).offset(currTileScreenPosition.x, currTileScreenPosition.y - 2 * tileFoorHeight);
			}
			else
			{
				//unit is not moving close to tile
				return;
			}
		}
	}
	else if (unitTile != currTile || unitFromAbove)
	{
		return;
	}

	Position tileScreenPosition;
	_camera->convertMapToScreen(unitTile->getPosition() + Position(0,0, (-unitFromBelow) + (+unitFromAbove)), &tileScreenPosition);
	tileScreenPosition += _camera->getMapOffset();

	//get shade helpers
	auto getTileShade = [&](Tile* tile)
	{
		return tile ? (tile->isDiscovered(O_FLOOR) ? reShade(tile) : 16) : 16;
	};
	auto getMixedTileShade = [&](Tile* tile, int heightOffset, bool below)
	{
		int shadeLower = 0;
		int shadeUpper = 0;
		if (below)
		{
			shadeLower = getTileShade(_save->getBelowTile(tile));
			shadeUpper = getTileShade(tile);
		}
		else
		{
			shadeLower = getTileShade(tile);
			shadeUpper = getTileShade(_save->getAboveTile(tile));
		}

		return Interpolate(shadeLower, shadeUpper, -heightOffset, Position::TileZ);
	};

	// draw unit
	auto shade = 0;
	auto offsets = calculateWalkingOffset(bu);
	if (moving)
	{
		const auto start = bu->getPosition();
		const auto end = bu->getDestination();
		const auto minLevel = std::min(start.z, end.z);
		const auto startShade = getMixedTileShade(_save->getTile(start), start.z == minLevel ? offsets.TerrainLevelOffset : 0, false);
		const auto endShade = getMixedTileShade(_save->getTile(end), end.z == minLevel ? offsets.TerrainLevelOffset : 0, false);
		shade = Interpolate(startShade, endShade, offsets.NormalizedMovePhase, 16);
	}
	else
	{
		shade = getMixedTileShade(currTile, offsets.TerrainLevelOffset, unitFromBelow);
		if (_showObstacles && unitTile->getObstacle(4))
		{
			shade = getShadePulseForFrame(shade, _animFrame);
		}
	}
	if (_debugVisionMode == 1)
	{
		shade = std::min(+NIGHT_VISION_SHADE, shade);
	}
	unitSprite.draw(bu, part, tileScreenPosition.x + offsets.ScreenOffset.x, tileScreenPosition.y + offsets.ScreenOffset.y, shade, mask, _isAltPressed);
}

/**
 * Draw the terrain.
 * Keep this function as optimised as possible. It's big to minimise overhead of function calls.
 * @param surface The surface to draw on.
 */
void Map::drawTerrain(Surface *surface)
{
	_isAltPressed = _game->isAltPressed(true);
	int frameNumber = 0;
	SurfaceRaw<const Uint8> tmpSurface;
	Tile *tile;
	int beginX = 0, endX = _save->getMapSizeX() - 1;
	int beginY = 0, endY = _save->getMapSizeY() - 1;
	int beginZ = 0, endZ = _save->getMapSizeZ() - 1;
	Position mapPosition, screenPosition, bulletPositionScreen, movingUnitPosition;
	int bulletLowX=16000, bulletLowY=16000, bulletLowZ=16000, bulletHighX=0, bulletHighY=0, bulletHighZ=0;
	int dummy;
	BattleUnit *movingUnit = _save->getTileEngine()->getMovingUnit();
	int tileShade, tileColor, obstacleShade;
	UnitSprite unitSprite(surface, _game->getMod(), _save, _animFrame, _save->getDepth() != 0);
	ItemSprite itemSprite(surface, _game->getMod(), _save, _animFrame);

	const int halfAnimFrame = (_animFrame / 2) % 4;
	const int halfAnimFrameRest = (_animFrame % 2);

	NumberText *_numWaypid = 0;

	// Highlight the boundaries of the map when editing
	if (_game->isState(_save->getMapEditorState()))
	{
		// Paints the ground floor of a map a different color to highlight it
		Uint8 color = Options::oxceMapEditorBoundsColor;
		Position topLeft, topRight, bottomLeft, bottomRight;
		// The extra offset of (2, 1, 0) was determined by testing
		// Why it's necessary in the first place? Some offset convention elsewhere in the code or something.
		_camera->convertMapToScreen(Position(2, 1, 0), &topLeft);
		_camera->convertMapToScreen(Position(_camera->getMapSizeX() + 2, 1, 0), &topRight);
		_camera->convertMapToScreen(Position(2, _camera->getMapSizeY() + 1, 0), &bottomLeft);
		_camera->convertMapToScreen(Position(_camera->getMapSizeX() + 2, _camera->getMapSizeY() + 1, 0), &bottomRight);

		topLeft += _camera->getMapOffset();
		topRight += _camera->getMapOffset();
		bottomLeft += _camera->getMapOffset();
		bottomRight += _camera->getMapOffset();

		Sint16 x[4] = {topLeft.x, topRight.x, bottomRight.x, bottomLeft.x};
		Sint16 y[4] = {topLeft.y, topRight.y, bottomRight.y, bottomLeft.y};
		surface->drawPolygon(x, y, 4, color);
	}

	// if we got bullet, get the highest x and y tiles to draw it on
	if (_projectile && _explosions.empty())
	{
		int part = _projectile->getItem() ? 0 : BULLET_SPRITES-1;
		for (int i = 0; i <= part; ++i)
		{
			if (_projectile->getPosition(1-i).x < bulletLowX)
				bulletLowX = _projectile->getPosition(1-i).x;
			if (_projectile->getPosition(1-i).y < bulletLowY)
				bulletLowY = _projectile->getPosition(1-i).y;
			if (_projectile->getPosition(1-i).z < bulletLowZ)
				bulletLowZ = _projectile->getPosition(1-i).z;
			if (_projectile->getPosition(1-i).x > bulletHighX)
				bulletHighX = _projectile->getPosition(1-i).x;
			if (_projectile->getPosition(1-i).y > bulletHighY)
				bulletHighY = _projectile->getPosition(1-i).y;
			if (_projectile->getPosition(1-i).z > bulletHighZ)
				bulletHighZ = _projectile->getPosition(1-i).z;
		}
		// divide by 16 to go from voxel to tile position
		bulletLowX = bulletLowX / 16;
		bulletLowY = bulletLowY / 16;
		bulletLowZ = bulletLowZ / 24;
		bulletHighX = bulletHighX / 16;
		bulletHighY = bulletHighY / 16;
		bulletHighZ = bulletHighZ / 24;

		// if the projectile is outside the viewport - center it back on it
		_camera->convertVoxelToScreen(_projectile->getPosition(), &bulletPositionScreen);

		if (_projectileInFOV && _followProjectile)
		{
			Position newCam = _camera->getMapOffset();
			if (newCam.z != bulletHighZ) //switch level
			{
				newCam.z = bulletHighZ;
				if (_projectileInFOV)
				{
					_camera->setMapOffset(newCam);
					_camera->convertVoxelToScreen(_projectile->getPosition(), &bulletPositionScreen);
				}
			}
			if (_smoothCamera)
			{
				if (_launch)
				{
					_launch = false;
					if ((bulletPositionScreen.x < 1 || bulletPositionScreen.x > surface->getWidth() - 1 ||
						bulletPositionScreen.y < 1 || bulletPositionScreen.y > _visibleMapHeight - 1))
					{
						_camera->centerOnPosition(Position(bulletLowX, bulletLowY, bulletHighZ), false);
						_camera->convertVoxelToScreen(_projectile->getPosition(), &bulletPositionScreen);
					}
				}
				if (!_smoothingEngaged)
				{
					if (bulletPositionScreen.x < 1 || bulletPositionScreen.x > surface->getWidth() - 1 ||
						bulletPositionScreen.y < 1 || bulletPositionScreen.y > _visibleMapHeight - 1)
					{
						_smoothingEngaged = true;
					}
				}
				else
				{
					_camera->jumpXY(surface->getWidth() / 2 - bulletPositionScreen.x, _visibleMapHeight / 2 - bulletPositionScreen.y);
				}
			}
			else
			{
				bool enough;
				do
				{
					enough = true;
					if (bulletPositionScreen.x < 0)
					{
						_camera->jumpXY(+surface->getWidth(), 0);
						enough = false;
					}
					else if (bulletPositionScreen.x > surface->getWidth())
					{
						_camera->jumpXY(-surface->getWidth(), 0);
						enough = false;
					}
					else if (bulletPositionScreen.y < 0)
					{
						_camera->jumpXY(0, +_visibleMapHeight);
						enough = false;
					}
					else if (bulletPositionScreen.y > _visibleMapHeight)
					{
						_camera->jumpXY(0, -_visibleMapHeight);
						enough = false;
					}
					_camera->convertVoxelToScreen(_projectile->getPosition(), &bulletPositionScreen);
				}
				while (!enough);
			}
		}
	}

	// get corner map coordinates to give rough boundaries in which tiles to redraw are
	_camera->convertScreenToMap(0, 0, &beginX, &dummy);
	_camera->convertScreenToMap(surface->getWidth(), 0, &dummy, &beginY);
	_camera->convertScreenToMap(surface->getWidth() + _spriteWidth, surface->getHeight() + _spriteHeight, &endX, &dummy);
	_camera->convertScreenToMap(0, surface->getHeight() + _spriteHeight, &dummy, &endY);
	beginY -= (_camera->getViewLevel() * 2);
	beginX -= (_camera->getViewLevel() * 2);
	if (beginX < 0)
		beginX = 0;
	if (beginY < 0)
		beginY = 0;

	if (!_camera->getShowAllLayers())
	{
		endZ = std::min(endZ, _camera->getViewLevel());
	}


	bool pathfinderTurnedOn = _save->getPathfinding()->isPathPreviewed();

	if (!_waypoints.empty() || (pathfinderTurnedOn && (_previewSettingTu || _previewSettingEnergy)) || 
		(_game->isState(_save->getMapEditorState()) && _save->getMapEditorState()->getRouteMode()))
	{
		_numWaypid = new NumberText(15, 15, 20, 30);
		_numWaypid->setPalette(getPalette());
		_numWaypid->setColor(pathfinderTurnedOn || _save->getMapEditorState()->getRouteMode() ? _messageColor + 1 : Palette::blockOffset(1));
	}

	if (movingUnit)
	{
		movingUnitPosition = movingUnit->getPosition();
	}

	surface->lock();
	const auto cameraPos = _camera->getMapOffset();
	for (int itZ = beginZ; itZ <= endZ; itZ++)
	{
		bool topLayer = itZ == endZ;
		for (int itY = beginY; itY < endY; itY++)
		{
			mapPosition = Position(beginX, itY, itZ);
			tile = _save->getTile(mapPosition);
			for (int itX = beginX; itX < endX; itX++, mapPosition.x++, tile++)
			{
				_camera->convertMapToScreen(mapPosition, &screenPosition);
				screenPosition += cameraPos;

				// only render cells that are inside the surface
				if (screenPosition.x > -_spriteWidth && screenPosition.x < surface->getWidth() + _spriteWidth &&
					screenPosition.y > -_spriteHeight && screenPosition.y < surface->getHeight() + _spriteHeight )
				{
					auto isUnitMovingNearby = movingUnit && positionInRangeXY(movingUnitPosition, mapPosition, 2);

					if (tile->isDiscovered(O_FLOOR))
					{
						tileShade = reShade(tile);
						obstacleShade = tileShade;
						if (_showObstacles)
						{
							if (tile->isObstacle())
							{
								obstacleShade = getShadePulseForFrame(tileShade, _animFrame);
							}
						}
					}
					else
					{
						tileShade = 16;
						obstacleShade = 16;
					}

					tileColor = tile->getMarkerColor();

					// Draw floor
					tmpSurface = tile->getSprite(O_FLOOR);
					if (tmpSurface)
					{
						if (tile->getObstacle(O_FLOOR))
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_FLOOR), obstacleShade, false, _nvColor);
						else
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_FLOOR), tileShade, false, _nvColor);
					}

					auto unit = tile->getUnit();

					// Draw cursor back
					if (_cursorType != CT_NONE && _selectorX > itX - _cursorSize && _selectorY > itY - _cursorSize && _selectorX < itX+1 && _selectorY < itY+1 &&
						((_save->getMapEditorState() && !_save->getMapEditorState()->getMouseOverIcons()) ||
						(_save->getBattleState() && !_save->getBattleState()->isMouseBeingUsed())))
					{
						if (_camera->getViewLevel() == itZ)
						{
							if (_cursorType != CT_AIM)
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = halfAnimFrameRest; // yellow box
								else
									frameNumber = 0; // red box
							}
							else
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = 7 + halfAnimFrame; // yellow animated crosshairs
								else
									frameNumber = 6; // red static crosshairs
							}
							tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
						}
						else if (_camera->getViewLevel() > itZ)
						{
							frameNumber = 2; // blue box
							tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
						}
					}

					if (isUnitMovingNearby)
					{
						// special handling for a moving unit in background of tile.
						constexpr static Position backPos[] =
						{
							Position(0, -1, 0),
							Position(-1, -1, 0),
							Position(-1, 0, 0),
						};

						for (size_t b = 0; b < std::size(backPos); ++b)
						{
							drawUnit(unitSprite, _save->getTile(mapPosition + backPos[b]), tile, screenPosition, topLayer);
						}
					}

					// Draw walls
					{
						// Draw west wall
						tmpSurface = tile->getSprite(O_WESTWALL);
						if (tmpSurface)
						{
							auto wallShade = getWallShade(O_WESTWALL, tile);
							if (tile->getObstacle(O_WESTWALL))
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_WESTWALL), obstacleShade, false, _nvColor);
							else
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_WESTWALL), wallShade, false, _nvColor);
						}
						// Draw north wall
						tmpSurface = tile->getSprite(O_NORTHWALL);
						if (tmpSurface)
						{
							auto wallShade = getWallShade(O_NORTHWALL, tile);
							if (tile->getObstacle(O_NORTHWALL))
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_NORTHWALL), obstacleShade, bool(tile->getSprite(O_WESTWALL)), _nvColor);
							else
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_NORTHWALL), wallShade, bool(tile->getSprite(O_WESTWALL)), _nvColor);
						}
						// Draw object
						tmpSurface = tile->getSprite(O_OBJECT);
						if (tmpSurface)
						{
							if (tile->isBackTileObject(O_OBJECT))
							{
								if (tile->getObstacle(O_OBJECT))
									Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_OBJECT), obstacleShade, false, _nvColor);
								else
									Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_OBJECT), tileShade, false, _nvColor);
							}
						}
						// draw an item on top of the floor (if any)
						BattleItem* item = tile->getTopItem();
						if (item)
						{
							itemSprite.draw(item,
								screenPosition.x,
								screenPosition.y + tile->getTerrainLevel(),
								tileShade
							);
							if (_anyIndicator)
							{
								BattleUnit *itemUnit = item->getUnit();
								if (itemUnit && itemUnit->getStatus() == STATUS_UNCONSCIOUS && itemUnit->indicatorsAreEnabled())
								{
									if (_burnIndicator && itemUnit->getFire() > 0)
									{
										_burnIndicator->blitNShade(surface,
											screenPosition.x,
											screenPosition.y + tile->getTerrainLevel(),
											tileShade);
									}
									else if (_woundIndicator && itemUnit->getFatalWounds() > 0)
									{
										_woundIndicator->blitNShade(surface,
											screenPosition.x,
											screenPosition.y + tile->getTerrainLevel(),
											tileShade);
									}
									else if (_shockIndicator && itemUnit->hasNegativeHealthRegen())
									{
										_shockIndicator->blitNShade(surface,
											screenPosition.x,
											screenPosition.y + tile->getTerrainLevel(),
											tileShade);
									}
									else if (_stunIndicator)
									{
										_stunIndicator->blitNShade(surface,
											screenPosition.x,
											screenPosition.y + tile->getTerrainLevel(),
											tileShade);
									}
								}
							}
						}
					}

					// check if we got bullet && it is in Field Of View
					if (_projectile && _projectileInFOV)
					{
						tmpSurface = nullptr;
						BattleItem* item = _projectile->getItem();
						if (item)
						{
							Position voxelPos = _projectile->getPosition();
							// draw shadow on the floor
							voxelPos.z = _save->getTileEngine()->castedShade(voxelPos);
							if (voxelPos.x / 16 >= itX &&
								voxelPos.y / 16 >= itY &&
								voxelPos.x / 16 <= itX+1 &&
								voxelPos.y / 16 <= itY+1 &&
								voxelPos.z / 24 == itZ &&
								_save->getTileEngine()->isVoxelVisible(voxelPos))
							{
								_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);

								itemSprite.drawShadow(item,
									bulletPositionScreen.x - 16,
									bulletPositionScreen.y - 26
								);
							}

							voxelPos = _projectile->getPosition();
							// draw thrown object
							if (voxelPos.x / 16 >= itX &&
								voxelPos.y / 16 >= itY &&
								voxelPos.x / 16 <= itX+1 &&
								voxelPos.y / 16 <= itY+1 &&
								voxelPos.z / 24 == itZ &&
								_save->getTileEngine()->isVoxelVisible(voxelPos))
							{
								_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);

								itemSprite.draw(item,
									bulletPositionScreen.x - 16,
									bulletPositionScreen.y - 26,
									tileShade
								);
							}
						}
						else
						{
							// draw bullet on the correct tile
							if (itX >= bulletLowX && itX <= bulletHighX && itY >= bulletLowY && itY <= bulletHighY)
							{
								int begin = 0;
								int end = BULLET_SPRITES;
								int direction = 1;
								if (_projectile->isReversed())
								{
									begin = BULLET_SPRITES - 1;
									end = -1;
									direction = -1;
								}

								for (int i = begin; i != end; i += direction)
								{
									tmpSurface = _projectileSet->getFrame(_projectile->getParticle(i));
									if (tmpSurface)
									{
										Position voxelPos = _projectile->getPosition(1-i);
										// draw shadow on the floor
										voxelPos.z = _save->getTileEngine()->castedShade(voxelPos);
										if (voxelPos.x / 16 == itX &&
											voxelPos.y / 16 == itY &&
											voxelPos.z / 24 == itZ &&
											_save->getTileEngine()->isVoxelVisible(voxelPos))
										{
											_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
											bulletPositionScreen.x -= tmpSurface.getWidth() / 2;
											bulletPositionScreen.y -= tmpSurface.getHeight() / 2;
											Surface::blitRaw(surface, tmpSurface, bulletPositionScreen.x, bulletPositionScreen.y, 16, false, _nvColor);
										}

										// draw bullet itself
										voxelPos = _projectile->getPosition(1-i);
										if (voxelPos.x / 16 == itX &&
											voxelPos.y / 16 == itY &&
											voxelPos.z / 24 == itZ &&
											_save->getTileEngine()->isVoxelVisible(voxelPos))
										{
											_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
											bulletPositionScreen.x -= tmpSurface.getWidth() / 2;
											bulletPositionScreen.y -= tmpSurface.getHeight() / 2;
											Surface::blitRaw(surface, tmpSurface, bulletPositionScreen.x, bulletPositionScreen.y, 0, false, _nvColor);
										}
									}
								}
							}
						}
					}

					//draw particle clouds
					int pixelMaskArray[] = { 0, 2, 1, 3 };
					SurfaceRaw<int> pixelMask(pixelMaskArray, 2, 2);
					const int vaporScreenOriginX = screenPosition.x + _spriteWidth / 2;
					const int vaporScreenOriginY = screenPosition.y + _spriteHeight - _spriteWidth / 2 + tile->getPosition().toVoxel().z;
					const Uint8* const transparetPtr = _transparencies->data();

					//draw particle clouds behind solder
					for (const Particle& p : getVaporParticle(tile, 0))
					{
						int vaporX = vaporScreenOriginX + p.getOffsetX();
						int vaporY = vaporScreenOriginY + p.getOffsetY();
						auto transparetOffsets = transparetPtr
							+ (p.getColor() * Mod::TransparenciesOpacityLevels * Mod::TransparenciesPaletteColors)
							+ (p.getOpacity() * Mod::TransparenciesPaletteColors);

						ShaderDrawFunc(
							[&](Uint8& dest, int size)
							{
								if (p.getSize() <= size)
								{
									dest = transparetOffsets[dest];
								}
							},
							ShaderSurface(this),
							ShaderMove(pixelMask, vaporX, vaporY)
						);
					}

					unit = tile->getUnit();
					// Draw soldier from this tile, below or above
					drawUnit(unitSprite, tile, tile, screenPosition, topLayer, isUnitMovingNearby ? movingUnit : nullptr);

					if (isUnitMovingNearby)
					{
						// special handling for a moving unit in foreground of tile.
						constexpr static Position frontPos[] =
						{
							Position(-1, +1, 0),
							Position(0, +1, 0),
							Position(+1, +1, 0),
							Position(+1, 0, 0),
							Position(+1, -1, 0),
						};

						for (size_t f = 0; f < std::size(frontPos); ++f)
						{
							drawUnit(unitSprite, _save->getTile(mapPosition + frontPos[f]), tile, screenPosition, topLayer);
						}
					}

					// Draw smoke/fire
					if (tile->getSmoke() && tile->isDiscovered(O_FLOOR))
					{
						frameNumber = 0;
						int shade = 0;
						if (!tile->getFire())
						{
							if (_save->getDepth() > 0)
							{
								frameNumber += Mod::UNDERWATER_SMOKE_OFFSET;
							}
							else
							{
								frameNumber += Mod::SMOKE_OFFSET;
							}
							frameNumber += int(floor((tile->getSmoke() / 6.0) - 0.1)); // see http://www.ufopaedia.org/images/c/cb/Smoke.gif
							shade = tileShade;
						}

						if (halfAnimFrame + tile->getAnimationOffset() > 3)
						{
							frameNumber += halfAnimFrame + tile->getAnimationOffset() - 4;
						}
						else
						{
							frameNumber += halfAnimFrame + tile->getAnimationOffset();
						}
						tmpSurface = _game->getMod()->getSurfaceSet("SMOKE.PCK")->getFrame(frameNumber);
						Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, shade, false, _nvColor);
					}

					//draw particle clouds on front of solder
					for (const Particle& p : getVaporParticle(tile, topLayer ? 3 : 1))
					{
						int vaporX = vaporScreenOriginX + p.getOffsetX();
						int vaporY = vaporScreenOriginY + p.getOffsetY();
						auto transparetOffsets = transparetPtr
							+ (p.getColor() * Mod::TransparenciesOpacityLevels * Mod::TransparenciesPaletteColors)
							+ (p.getOpacity() * Mod::TransparenciesPaletteColors);

						ShaderDrawFunc(
							[&](Uint8& dest, int size)
							{
								if (p.getSize() <= size)
								{
									dest = transparetOffsets[dest];
								}
							},
							ShaderSurface(this),
							ShaderMove(pixelMask, vaporX, vaporY)
						);
					}

					// Draw Path Preview
					if (_previewSettingArrows && tile->getPreview() != -1 && tile->isDiscovered(O_FLOOR))
					{
						if (itZ > 0 && tile->hasNoFloor(_save))
						{
							tmpSurface = _game->getMod()->getSurfaceSet("Pathfinding")->getFrame(11);
							if (tmpSurface)
							{
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y+2, 0, false, tile->getMarkerColor());
							}
						}
						tmpSurface = _game->getMod()->getSurfaceSet("Pathfinding")->getFrame(tile->getPreview());
						if (tmpSurface)
						{
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y + tile->getTerrainLevel(), 0, false, tileColor);
						}
					}

					{
						// Draw object
						tmpSurface = tile->getSprite(O_OBJECT);
						if (tmpSurface)
						{
							if (!tile->isBackTileObject(O_OBJECT))
							{
								if (tile->getObstacle(O_OBJECT))
									Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_OBJECT), obstacleShade, false, _nvColor);
								else
									Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - tile->getYOffset(O_OBJECT), tileShade, false, _nvColor);
							}
						}
					}
					// Draw cursor front
					if (_cursorType != CT_NONE && _selectorX > itX - _cursorSize && _selectorY > itY - _cursorSize && _selectorX < itX+1 && _selectorY < itY+1 &&
						((_save->getMapEditorState() && !_save->getMapEditorState()->getMouseOverIcons()) ||
						(_save->getBattleState() && !_save->getBattleState()->isMouseBeingUsed())))
					{
						if (_camera->getViewLevel() == itZ)
						{
							if (_cursorType != CT_AIM)
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = 3 + halfAnimFrameRest; // yellow box
								else
									frameNumber = 3; // red box
							}
							else
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = 7 + halfAnimFrame; // yellow animated crosshairs
								else
									frameNumber = 6; // red static crosshairs
							}
							tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);

							// UFO extender accuracy: display adjusted accuracy value on crosshair in real-time.
							if ((_cursorType == CT_AIM || _cursorType == CT_PSI || _cursorType == CT_WAYPOINT) && Options::battleUFOExtenderAccuracy)
							{
								bool cacheIsUpdated = false;

								BattleAction *action = _save->getBattleGame()->getCurrentAction();
								const RuleItem *weapon = action->weapon->getRules();
								std::ostringstream ss;
								auto attack = BattleActionAttack::GetBeforeShoot(*action);
								int distanceSq = action->actor->distance3dToPositionSq(Position(itX, itY,itZ));
								int distance = (int)std::ceil(sqrt(float(distanceSq)));

								if (_cursorType == CT_AIM)
								{
									int accuracy = BattleUnit::getFiringAccuracy(attack, _game->getMod());
									int upperLimit = 200;
									int lowerLimit = weapon->getMinRange();
									switch (action->type)
									{
									case BA_AIMEDSHOT:
										upperLimit = weapon->getAimRange();
										break;
									case BA_SNAPSHOT:
										upperLimit = weapon->getSnapRange();
										break;
									case BA_AUTOSHOT:
										upperLimit = weapon->getAutoRange();
										break;
									default:
										break;
									}
									// at this point, let's assume the shot is adjusted and set the text amber.
									_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::yellow - 1) - 1);

									if (distance > upperLimit)
									{
										accuracy -= (distance - upperLimit) * weapon->getDropoff();
									}
									else if (distance < lowerLimit)
									{
										accuracy -= (lowerLimit - distance) * weapon->getDropoff();
									}
									else
									{
										// no adjustment made? set it to green.
										_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::green - 1) - 1);
									}

									// Include LOS penalty for tiles in the unit's current view range
									// Don't recalculate LOS for outside of the current FOV
									int noLOSAccuracyPenalty = action->weapon->getRules()->getNoLOSAccuracyPenalty(_game->getMod());
									if (noLOSAccuracyPenalty != -1)
									{
										bool isCtrlPressed = _game->isCtrlPressed(true);
										bool hasLOS = false;
										if (Position(itX, itY, itZ) == _cacheCursorPosition && isCtrlPressed == _cacheIsCtrlPressed && _cacheHasLOS != -1)
										{
											// use cached result
											hasLOS = (_cacheHasLOS == 1);
										}
										else
										{
											// recalculate
											if (unit && (unit->getVisible() || _save->getDebugMode()))
											{
												hasLOS = _save->getTileEngine()->visible(action->actor, tile);
											}
											else
											{
												hasLOS = _save->getTileEngine()->isTileInLOS(action, tile);
											}
											// remember
											_cacheIsCtrlPressed = isCtrlPressed;
											_cacheCursorPosition = Position(itX, itY, itZ);
											_cacheHasLOS = hasLOS ? 1 : 0;
											cacheIsUpdated = true;
										}

										if (!hasLOS)
										{
											accuracy = accuracy * noLOSAccuracyPenalty / 100;
											_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::yellow - 1) - 1);
										}
									}

									if ( Options::battleRealisticAccuracy )
									{
										BattleUnit* shooterUnit = action->actor;

										const bool isCtrlPressed = _game->isCtrlPressed(true); // Just in case it'll be used sometimes
										const bool isKneeled = shooterUnit->isKneeled();

										if (Position(itX, itY, itZ) == _cacheCursorPosition
											&& isCtrlPressed == _cacheIsCtrlPressed
											&& isKneeled == _cacheIsKneeled
											&& _cacheAccuracy != -1
											&& !cacheIsUpdated)

										{
											// use cached result
											accuracy = _cacheAccuracy;
										}
										else
										{
											int targetSize = 1;
											int maxVoxels = 0;
											double maxExposure = 0.0;
											int distance_in_tiles = 0;
											Tile *target = nullptr;
											std::vector<Position> exposedVoxels;

											if (unit) // Targeting unit
											{
												targetSize = unit->getArmor()->getSize();
												exposedVoxels.reserve(( 1 + TileEngine::maxBigUnitRadius * 2) * TileEngine::voxelTileSize.z / 2 ); // this much
												target = unit->getTile();
												action->target = target->getPosition();
												BattleActionOrigin tempOrigin = action->relativeOrigin;

												for (const auto &relPos : { BattleActionOrigin::CENTRE, BattleActionOrigin::LEFT, BattleActionOrigin::RIGHT })
												{
													exposedVoxels.clear();
													action->relativeOrigin = relPos;
													Position origin = _save->getTileEngine()->getOriginVoxel(*action, shooterUnit->getTile());
													double exposure = _save->getTileEngine()->checkVoxelExposure(&origin, target, shooterUnit, false, &exposedVoxels, false);

													if ((int)exposedVoxels.size() > maxVoxels)
													{
														maxVoxels = exposedVoxels.size();
														maxExposure = exposure;
													}
												}
												action->relativeOrigin = tempOrigin;
												accuracy = (int)ceil((double)accuracy * maxExposure);
											}
											else
											{
												target = _save->getTile(Position(itX, itY, itZ)); // We are targeting empty terrain tile
												action->target = target->getPosition();
											}

											if ( unit && unit == shooterUnit)
											{
												accuracy = 100;
											}
											else if ( unit && maxVoxels == 0)
											{
												accuracy = 0;
											}
											else
											{
												Position origin = _save->getTileEngine()->getOriginVoxel( *action, shooterUnit->getTile());
												int xdiff = origin.x/16 - target->getPosition().x;
												int ydiff = origin.y/16 - target->getPosition().y;

												double zdiff = (origin.z/24 - target->getPosition().z)*1.5;
												distance_in_tiles = (int)floor(sqrt((double)(xdiff*xdiff + ydiff*ydiff + zdiff*zdiff))); // Distance in cube 16x16x16 tiles

												if (distance_in_tiles==0) accuracy = 100;

												else if (distance_in_tiles <= 10 && weapon->getMinRange() == 0 && action->type == BA_AIMEDSHOT) // For aimed shot...
												{
													if (accuracy*2 >= 100)
														accuracy = std::min(100, (int)ceil(accuracy*(2-((double)distance_in_tiles-1)/10))); // Multiplier x1.1..x2 for 10 tiles, nearest to target
													else
														accuracy += (100 - accuracy)/distance_in_tiles; // Or just evenly divide to get 100% accuracy on tile adjanced to a target
												}

												else if (distance_in_tiles <= 5 && weapon->getMinRange() == 0 && (action->type == BA_AUTOSHOT || action->type == BA_SNAPSHOT)) // For snap/auto
												{
													if (accuracy*2 >= 100)
														accuracy = std::min(100, (int)ceil(accuracy*(2-((double)distance_in_tiles-1)/5))); // Multiplier x1.2..x2 for 5 nearest tiles
													else
														accuracy += (100 - accuracy)/distance_in_tiles;
												}

												if (accuracy <= AccuracyMod.MinCap) // Rule for difficult/long-range shots
												{
													accuracy = AccuracyMod.MinCap;
													int hardShotAccuracy = (int)(maxExposure / targetSize * 100);
													if (hardShotAccuracy > 0 && hardShotAccuracy < AccuracyMod.MinCap) accuracy = hardShotAccuracy; // Accuracy can be below minimal cap for covered targets
													if (isKneeled) accuracy += AccuracyMod.KneelBonus; // And let's make kneeling more meaningful for such shots
													if (action->type == BA_AIMEDSHOT) accuracy += AccuracyMod.AimBonus;
													_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::red - 1) - 1);
												}
												else if (accuracy > AccuracyMod.MaxCap)
												{
													accuracy = AccuracyMod.MaxCap;
												}

												bool outOfRange = weapon->isOutOfRange(distanceSq);
												// zero accuracy or out of range: set it red.
												if (accuracy <= 0 || outOfRange)
												{
													accuracy = 0;
													_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::red - 1) - 1);
												}
											}

											// remember
											_cacheCursorPosition = Position(itX, itY, itZ);
											_cacheAccuracy = accuracy;
											_cacheIsKneeled = isKneeled;
										}
									}
									else
									{
										bool outOfRange = weapon->isOutOfRange(distanceSq);
										// zero accuracy or out of range: set it red.
										if (accuracy <= 0 || outOfRange)
										{
											accuracy = 0;
											_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::red - 1) - 1);
										}
									}

									ss << accuracy;
									ss << "%";
								}
								//TODO: merge this code with `InventoryState::calculateCurrentDamageTooltip` as 90% is same or should be same
								// display additional damage and psi-effectiveness info
								if (_isAltPressed)
								{
									// step 1: determine rule
									const RuleItem *rule;
									if (weapon->getBattleType() == BT_PSIAMP)
									{
										rule = weapon;
									}
									else if (action->weapon->needsAmmoForAction(action->type))
									{
										auto ammo = attack.damage_item;
										if (ammo != nullptr)
										{
											rule = ammo->getRules();
										}
										else
										{
											rule = 0; // empty weapon = no rule
										}
									}
									else
									{
										rule = weapon;
									}

									// step 2: check if unlocked
									if (_cacheActiveWeaponUfopediaArticleUnlocked == -1)
									{
										_cacheActiveWeaponUfopediaArticleUnlocked = 0;
										if (_game->getSavedGame()->getMonthsPassed() == -1)
										{
											_cacheActiveWeaponUfopediaArticleUnlocked = 1; // new battle mode
										}
										else if (rule)
										{
											_cacheActiveWeaponUfopediaArticleUnlocked = 1; // assume unlocked
											ArticleDefinition *article = _game->getMod()->getUfopaediaArticle(rule->getType(), false);
											if (article && !Ufopaedia::isArticleAvailable(_game->getSavedGame(), article))
											{
												_cacheActiveWeaponUfopediaArticleUnlocked = 0; // ammo/weapon locked
											}
											if (rule->getType() != weapon->getType())
											{
												article = _game->getMod()->getUfopaediaArticle(weapon->getType(), false);
												if (article && !Ufopaedia::isArticleAvailable(_game->getSavedGame(), article))
												{
													_cacheActiveWeaponUfopediaArticleUnlocked = 0; // weapon locked
												}
											}
										}
									}

									// step 3: calculate and draw
									if (rule && _cacheActiveWeaponUfopediaArticleUnlocked == 1)
									{
										if (rule->getBattleType() == BT_PSIAMP)
										{
											float attackStrength = BattleUnit::getPsiAccuracy(attack);
											float defenseStrength = 30.0f; // indicator ignores: +victim->getArmor()->getPsiDefence(victim);

											float dis = Position::distance(action->actor->getPosition().toVoxel(), Position(itX, itY, itZ).toVoxel());
											int min = attackStrength - defenseStrength - rule->getPsiAccuracyRangeReduction(dis);
											int max = min + 55;
											if (max <= 0)
											{
												ss << "0%";
											}
											else
											{
												ss << min << "-" << max << "%";
											}
										}
										if (rule->getBattleType() != BT_PSIAMP || action->type == BA_USE)
										{
											int totalDamage = 0;
											totalDamage += rule->getPowerBonus(attack);
											totalDamage -= rule->getPowerRangeReduction(distance * 16);
											if (totalDamage < 0) totalDamage = 0;
											if (_cursorType != CT_WAYPOINT)
												ss << "\n";
											ss << rule->getDamageType()->getRandomDamage(totalDamage, 1);
											ss << "-";
											ss << rule->getDamageType()->getRandomDamage(totalDamage, 2);
											if (rule->getDamageType()->RandomType == DRT_UFO_WITH_TWO_DICE)
												ss << "*";
										}
									}
									else
									{
										ss << "\n?-?";
									}
								}

								_txtAccuracy->setText(ss.str());
								_txtAccuracy->draw();
								_txtAccuracy->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
							}
						}
						else if (_camera->getViewLevel() > itZ)
						{
							frameNumber = 5; // blue box
							tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
							Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
						}
						if (!_isAltPressed && _cursorType > CT_AIM && _camera->getViewLevel() == itZ)
						{
							bool ignore = false;
							if (_cursorType == CT_PSI || _cursorType == CT_WAYPOINT)
							{
								BattleAction* action = _save->getBattleGame()->getCurrentAction();
								int distanceSq = action->actor->distance3dToPositionSq(Position(itX, itY, itZ));
								if (action->weapon->getRules()->isOutOfRange(distanceSq))
								{
									// weapon doesn't work at this distance, just draw a normal cursor with a red 0% hint text
									ignore = true;
									_txtAccuracy->setColor(Palette::blockOffset(Pathfinding::red - 1) - 1);
									_txtAccuracy->setText("0%");
									_txtAccuracy->draw();
									_txtAccuracy->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
								}
							}
							if (!ignore)
							{
								int frame[6] = { 0, 0, 0, 11, 13, 15 };
								tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(frame[_cursorType] + (_animFrame / 4) % 2);
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
							}
						}
					}

					// Draw waypoints if any on this tile
					int waypid = 1;
					int waypXOff = 2;
					int waypYOff = 2;

					for (const auto& waypoint : _waypoints)
					{
						if (waypoint == mapPosition)
						{
							if (waypXOff == 2 && waypYOff == 2)
							{
								tmpSurface = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(7);
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
							}
							if (_save->getBattleGame()->getCurrentAction()->type == BA_LAUNCH || _save->getBattleGame()->getCurrentAction()->sprayTargeting)
							{
								_numWaypid->setValue(waypid);
								_numWaypid->draw();
								_numWaypid->blitNShade(surface, screenPosition.x + waypXOff, screenPosition.y + waypYOff, 0);

								waypXOff += waypid > 9 ? 8 : 6;
								if (waypXOff >= 26)
								{
									waypXOff = 2;
									waypYOff += 8;
								}
							}
						}
						waypid++;
					}
				}
			}
		}
	}
	if (pathfinderTurnedOn)
	{
		if (_numWaypid)
		{
			_numWaypid->setBordered(true); // give it a border for the pathfinding display, makes it more visible on snow, etc.
		}
		for (int itZ = beginZ; itZ <= endZ; itZ++)
		{
			for (int itX = beginX; itX <= endX; itX++)
			{
				for (int itY = beginY; itY <= endY; itY++)
				{
					mapPosition = Position(itX, itY, itZ);
					_camera->convertMapToScreen(mapPosition, &screenPosition);
					screenPosition += _camera->getMapOffset();

					// only render cells that are inside the surface
					if (screenPosition.x > -_spriteWidth && screenPosition.x < surface->getWidth() + _spriteWidth &&
						screenPosition.y > -_spriteHeight && screenPosition.y < surface->getHeight() + _spriteHeight )
					{
						tile = _save->getTile(mapPosition);
						if (!tile || !tile->isDiscovered(O_FLOOR) || tile->getPreview() == -1)
							continue;
						int adjustment = -tile->getTerrainLevel();
						if (_previewSettingArrows)
						{
							if (itZ > 0 && tile->hasNoFloor(_save))
							{
								tmpSurface = _game->getMod()->getSurfaceSet("Pathfinding")->getFrame(23);
								if (tmpSurface)
								{
									Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y+2, 0, false, tile->getMarkerColor());
								}
							}
							int overlay = tile->getPreview() + 12;
							tmpSurface = _game->getMod()->getSurfaceSet("Pathfinding")->getFrame(overlay);
							if (tmpSurface)
							{
								Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y - adjustment, 0, false, tile->getMarkerColor());
							}
						}

						if ((_previewSettingTu || _previewSettingEnergy) && (tile->getTUMarker() > -1 || tile->getEnergyMarker() > -1))
						{
							int off = tile->getTUMarker() > 9 ? 5 : 3;
							int offE = tile->getEnergyMarker() > 9 ? 5 : 3;
							int mcolor = _previewSettingArrows ? 0 : tile->getMarkerColor();
							if (_previewSettingArrows)
							{
								adjustment += 7;
							}
							if (_save->getSelectedUnit() && _save->getSelectedUnit()->isBigUnit())
							{
								adjustment += 1;
								if (!_previewSettingArrows)
								{
									adjustment += 7;
								}
							}
							if (_previewSettingTu)
							{
								_numWaypid->setValue(tile->getTUMarker());
								_numWaypid->draw();
								if (_previewSettingEnergy)
								{
									// TU
									_numWaypid->blitNShade(surface, screenPosition.x + 16 - off, screenPosition.y + (22 - adjustment), 0, false, mcolor);
									// and Energy
									_numWaypid->setValue(tile->getEnergyMarker());
									_numWaypid->draw();
									_numWaypid->blitNShade(surface, screenPosition.x + 16 - offE, screenPosition.y + (29 - adjustment), 0, false, mcolor);
								}
								else
								{
									// only TU
									_numWaypid->blitNShade(surface, screenPosition.x + 16 - off, screenPosition.y + (29 - adjustment), 0, false, mcolor);
								}
							}
							else if (_previewSettingEnergy)
							{
								// only Energy
								_numWaypid->setValue(tile->getEnergyMarker());
								_numWaypid->draw();
								_numWaypid->blitNShade(surface, screenPosition.x + 16 - offE, screenPosition.y + (29 - adjustment), 0, false, mcolor);
							}
						}
					}
				}
			}
		}
		if (_numWaypid)
		{
			_numWaypid->setBordered(false); // make sure we remove the border in case it's being used for missile waypoints.
		}
	}

	auto selectedUnit = _save->getSelectedUnit();
	if (selectedUnit && (_save->getSide() == FACTION_PLAYER || _save->getDebugMode()) && selectedUnit->getPosition().z <= _camera->getViewLevel())
	{
		_camera->convertMapToScreen(selectedUnit->getPosition(), &screenPosition);
		screenPosition += _camera->getMapOffset();
		Position offset = calculateWalkingOffset(selectedUnit).ScreenOffset;
		if (selectedUnit->isBigUnit())
		{
			offset.y += 4;
		}
		offset.y += Position::TileZ - (selectedUnit->getHeight() + selectedUnit->getFloatHeight());
		if (selectedUnit->isKneeled())
		{
			offset.y -= 2;
		}
		if (this->getCursorType() != CT_NONE)
		{
			_arrow->blitNShade(surface, screenPosition.x + offset.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2), screenPosition.y + offset.y - _arrow->getHeight() + getArrowBobForFrame(_animFrame), 0);
		}
	}

	// Draw motion scanner arrows
	if (_isAltPressed && _save->getSide() == FACTION_PLAYER && this->getCursorType() != CT_NONE)
	{
		for (auto myUnit : *_save->getUnits())
		{
			if (myUnit->getScannedTurn() == _save->getTurn() && myUnit->getFaction() != FACTION_PLAYER && !myUnit->isOut())
			{
				Position temp = myUnit->getPosition();
				temp.z = _camera->getViewLevel();
				_camera->convertMapToScreen(temp, &screenPosition);
				screenPosition += _camera->getMapOffset();
				Position offset;
				//calculateWalkingOffset(myUnit, &offset);
				if (myUnit->isBigUnit())
				{
					offset.y += 4;
				}
				offset.y += 24 - myUnit->getHeight();
				if (myUnit->isKneeled())
				{
					offset.y -= 2;
				}
				_arrow->blitNShade(
					surface,
					screenPosition.x + offset.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2),
					screenPosition.y + offset.y - _arrow->getHeight() + getArrowBobForFrame(_animFrame),
					0);
			}
		}
	}

	// Draw markers for nodes in the map editor
	if (_game->isState(_save->getMapEditorState()))
	{
		int markerFrame;

		if (_save->getMapEditorState()->getRouteMode())
		{
			// First draw node markers and lines
			for (auto node : *_save->getNodes())
			{
				if (!_game->getMapEditor()->isNodeActive(node))
				{
					continue;
				}

				Position nodePos = node->getPosition();
				_camera->convertMapToScreen(nodePos, &screenPosition);
				screenPosition += _camera->getMapOffset();

				std::vector<Node*>::iterator it = find(_game->getMapEditor()->getSelectedNodes()->begin(), _game->getMapEditor()->getSelectedNodes()->end(), node);
				bool selected = it != _game->getMapEditor()->getSelectedNodes()->end();
				//int Pathfinding::red = 3;
				//int Pathfinding::yellow = 10;
				//int Pathfinding::green = 4;
				// pick green as 'normal' color
				int markerColor = 4;
				if (_game->getMapEditor()->isNodeOverIDLimit(node))
				{
					// color node red if it won't be saved due to being over the ID limit
					markerColor = 3;
				}
				else if (selected)
				{
					// color node orange if selected and not over ID limit
					markerColor = 2;
				}

				// Node is above the current camera level: draw blue cursor up to its height if enabled
				Surface *cursorBack = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(2);
				Surface *cursorFront = _game->getMod()->getSurfaceSet("CURSOR.PCK")->getFrame(5);
				if (nodePos.z > _camera->getViewLevel() && Options::mapEditorShowOutOfPlaneOffsetCursor && (selected || Options::mapEditorShowOutOfPlaneNodes))
				{
					Position cursorPosition;

					if (cursorBack && cursorFront)
					{
						for (int zz = _camera->getViewLevel(); zz < nodePos.z; ++zz)
						{
							_camera->convertMapToScreen(Position(nodePos.x, nodePos.y, zz), &cursorPosition);
							cursorPosition += _camera->getMapOffset();
							Surface::blitRaw(surface, cursorBack, cursorPosition.x, cursorPosition.y, 0);
							Surface::blitRaw(surface, cursorFront, cursorPosition.x, cursorPosition.y, 0);
						}
					}
				}

				// Show nodes outside the current level as dashed/"transparent" marker
				// Or if the option is turned off, don't show them at all
				markerFrame = 10;
				if (nodePos.z != _camera->getViewLevel())
				{
					if (Options::mapEditorShowOutOfPlaneNodes || selected)
					{
						markerFrame += 12;
					}
					else if (!selected)
					{
						continue;
					}
				}

				tmpSurface = _game->getMod()->getSurfaceSet("Pathfinding")->getFrame(markerFrame);
				if (tmpSurface)
				{
					Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0, false, markerColor);
				}

				// Node is below the current camera level: draw blue cursor up to its height if enabled
				if (nodePos.z < _camera->getViewLevel() && Options::mapEditorShowOutOfPlaneOffsetCursor && (selected || Options::mapEditorShowOutOfPlaneNodes))
				{
					Position cursorPosition;

					if (cursorBack && cursorFront)
					{
						for (int zz = nodePos.z; zz < _camera->getViewLevel(); ++zz)
						{
							_camera->convertMapToScreen(Position(nodePos.x, nodePos.y, zz), &cursorPosition);
							cursorPosition += _camera->getMapOffset();
							Surface::blitRaw(surface, cursorBack, cursorPosition.x, cursorPosition.y, 0);
							Surface::blitRaw(surface, cursorFront, cursorPosition.x, cursorPosition.y, 0);
						}
					}
				}

				// Only draw links if the options allow us to
				if (!selected && Options::mapEditorShowLinksOnlyForSelectedNodes)
				{
					continue;
				}
				
				// Draw lines and arrows for connections between nodes
				Position startLinePos = screenPosition;
				startLinePos.x += _spriteWidth / 2;
				startLinePos.y += _spriteHeight * 4 / 5;
				for (int i = 0; i < 5; ++i)
				{
					int linkID = node->getNodeLinks()->at(i);
					Position linkPosition = Position(-1, -1, -1);
					Position endLinePos = startLinePos;
					bool exit = false;
					bool overIDLimit = _game->getMapEditor()->isNodeOverIDLimit(node);
					// line to another node
					if (linkID >= 0)
					{
						Node *otherNode = _save->getNodes()->at(linkID);
						if (!_game->getMapEditor()->isNodeActive(otherNode))
						{
							continue;
						}

						linkPosition = otherNode->getPosition();
						if (linkPosition.z != _camera->getViewLevel() && !Options::mapEditorShowOutOfPlaneNodeLinks && !selected)
						{
							continue;
						}

						overIDLimit |= _game->getMapEditor()->isNodeOverIDLimit(otherNode);
					}
					// exit north
					else if (linkID == -2)
					{
						linkPosition = Position(_save->getMapSizeX() / 2, -5, 0);
						exit = true;
					}
					// exit east
					else if (linkID == -3)
					{
						linkPosition = Position(_save->getMapSizeX() + 5, _save->getMapSizeY() / 2, 0);
						exit = true;
					}
					// exit south
					else if (linkID == -4)
					{
						linkPosition = Position(_save->getMapSizeX() / 2, _save->getMapSizeY() + 5, 0);
						exit = true;
					}
					// exit west
					else if (linkID == -5)
					{
						linkPosition = Position(-5, _save->getMapSizeY() / 2, 0);
						exit = true;
					}

					if (linkPosition != Position(-1, -1, -1))
					{
						// (color group * 16) + shade
						// green for 'normal' links
						Uint8 green = 3 * 16 + 4;
						Uint8 red = 2 * 16 + 4;
						Uint8 orange = 1 * 16 + 4;
						Uint8 lineColor = green;
						if (overIDLimit)
						{
							// won't be saved due to ID limit: red
							lineColor = red;
						}
						else if (selected)
						{
							// node is selected: orange
							lineColor = orange;
						}
						// draw line to other node
						_camera->convertMapToScreen(linkPosition, &screenPosition);
						screenPosition += _camera->getMapOffset();
						endLinePos = screenPosition;
						endLinePos.x += _spriteWidth / 2;
						endLinePos.y += _spriteHeight * 4 / 5;

						// draw dotted lines to out-of-plane nodes when told by options
						if (!exit && Options::mapEditorDottedOutOfPlaneNodeLinks && (linkPosition.z != nodePos.z || nodePos.z != _camera->getViewLevel()))
						{
							int segmentLength = 4;
							int numberOfSegments = Position::distance2d(startLinePos, endLinePos) / segmentLength;
							Position slope = endLinePos - startLinePos;
							for (int j = 0; j < numberOfSegments; j += 2)
							{
								Position segmentStart = startLinePos + slope * j / numberOfSegments;
								Position segmentEnd = startLinePos + slope * (j + 1) / numberOfSegments;
								surface->drawLine(segmentStart.x, segmentStart.y, segmentEnd.x, segmentEnd.y, lineColor);
							}
						}
						// draw solid lines to exits, in-plane nodes, and when told by options
						else
						{
							surface->drawLine(startLinePos.x, startLinePos.y, endLinePos.x, endLinePos.y, lineColor);
						}

						// draw triangle for arrow showing direction of connection
						Sint16 offset = 10; // move the arrow slightly off of the center of the node marker to not overlap as much
						Sint16 length = 10;
						Sint16 width = 3;

						int x1 = startLinePos.x;
						int x2 = endLinePos.x;
						int y1 = startLinePos.y;
						int y2 = endLinePos.y;

						// start by determining the angle of the line we drew
						float angle = atan2(y2 - y1, x2 - x1);
						float cc = cos(angle);
						float ss = sin(angle);

						// rotate points of a triangle to face the node and shift them to the position of the node
						Sint16 arrowX[3] = {-offset, -(offset + length), -(offset + length)};
						Sint16 arrowY[3] = {0, width, -width};
						Sint16 arrayX[3];
						Sint16 arrayY[3];
						for (int j = 0; j < 3; ++j)
						{
							arrayX[j] = (float)arrowX[j] * cc - (float)arrowY[j] * ss;
							arrayY[j] = (float)arrowX[j] * ss + (float)arrowY[j] * cc;
							arrayX[j] += x2;
							arrayY[j] += y2;
						}

						// now actually draw
						surface->drawPolygon(arrayX, arrayY, 3, lineColor);
					}
				}
				
				//if (_game->getMapEditor()->getSelectedNodes()->size() > 0)
				//_arrow->blitNShade(surface, screenPosition.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2), screenPosition.y + (_spriteWidth * 1 / 5) - _arrow->getHeight() + getArrowBobForFrame(_animFrame), 0);
			}

			// Next add numbers for node IDs, spawn priority, and spawn rank
			for (auto node : *_save->getNodes())
			{
				if (!_game->getMapEditor()->isNodeActive(node))
				{
					continue;
				}

				Position nodePos = node->getPosition();
				_camera->convertMapToScreen(nodePos, &screenPosition);
				screenPosition += _camera->getMapOffset();

				std::vector<Node*>::iterator it = find(_game->getMapEditor()->getSelectedNodes()->begin(), _game->getMapEditor()->getSelectedNodes()->end(), node);
				bool selected = it != _game->getMapEditor()->getSelectedNodes()->end();

				if (nodePos.z != _camera->getViewLevel() && !Options::mapEditorShowOutOfPlaneNodes && !selected)
				{
					continue;
				}

				// Add numbers over each node to indicate their ID
				int off = node->getID() > 9 ? 5 : 3;
				_numWaypid->setBordered(true);
				_numWaypid->setValue(node->getID());
				_numWaypid->draw();
				_numWaypid->blitNShade(surface, screenPosition.x + 16 - off, screenPosition.y + 29, 0, false, 0);

				// Add numbers for spawn priority and rank
				_numWaypid->setValue(node->getPriority());
				_numWaypid->draw();
				_numWaypid->blitNShade(surface, screenPosition.x + 3, screenPosition.y + 16, 0, false, 0);
				_numWaypid->setValue(node->getRank());
				_numWaypid->draw();
				_numWaypid->blitNShade(surface, screenPosition.x + 3, screenPosition.y + 16 + 8, 0, false, 0);
			}
		}
		else
		{
			// draw tile selections
			for(auto selectedTile : *_game->getMapEditor()->getSelectedTiles())
			{
				Position tilePos = selectedTile->getPosition();
				_camera->convertMapToScreen(tilePos, &screenPosition);
				screenPosition += _camera->getMapOffset();

				markerFrame = 29;
				tmpSurface = _game->getMod()->getSurfaceSet("MapEditorIcons")->getFrame(markerFrame);
				if (tmpSurface)
				{
					Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
				}
			}
		}

		// draw cursor modes
		if (_save->getMapEditorState()->isMouseScrollSelecting())
		{
			Position tilePos = _save->getMapEditorState()->getScrollStartPosition();
			_camera->convertMapToScreen(tilePos, &screenPosition);
			screenPosition += _camera->getMapOffset();

			markerFrame = 30;
			tmpSurface = _game->getMod()->getSurfaceSet("MapEditorIcons")->getFrame(markerFrame);
			if (tmpSurface)
			{
				Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
			}

			getSelectorPosition(&tilePos);
			_camera->convertMapToScreen(tilePos, &screenPosition);
			screenPosition += _camera->getMapOffset();

			if (tmpSurface)
			{
				Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
			}
		}

		Position cursorPosition;
		getSelectorPosition(&cursorPosition);
		_camera->convertMapToScreen(cursorPosition, &screenPosition);
		screenPosition += _camera->getMapOffset();

		markerFrame = 31;
		tmpSurface = _game->getMod()->getSurfaceSet("MapEditorIcons")->getFrame(markerFrame);
		if (tmpSurface && _save->getMapEditorState()->isMouseScrollSelectionPainting())
		{
			Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
		}

		bool ctrlPressed = (SDL_GetModState() & KMOD_CTRL) != 0;
		bool shiftPressed = (SDL_GetModState() & KMOD_SHIFT) != 0;
		if (ctrlPressed || shiftPressed)
		{
			if (ctrlPressed)
			{
				markerFrame += 2;
			}

			if (shiftPressed)
			{
				markerFrame += 1;
			}

			tmpSurface = _game->getMod()->getSurfaceSet("MapEditorIcons")->getFrame(markerFrame);
			if (tmpSurface)
			{
				Surface::blitRaw(surface, tmpSurface, screenPosition.x, screenPosition.y, 0);
			}
		}
	}

	delete _numWaypid;

	// Draw craft deployment preview arrows
	if (_isAltPressed && _save->isPreview() && this->getCursorType() != CT_NONE)
	{
		for (auto& pos : _save->getCraftTiles())
		{
			if (pos.z == _camera->getViewLevel())
			{
				_camera->convertMapToScreen(pos, &screenPosition);
				screenPosition += _camera->getMapOffset();
				screenPosition.y += 2; // based on vanilla soldier standHeight
				_arrow->blitNShade(
					surface,
					screenPosition.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2),
					screenPosition.y - _arrow->getHeight() + getArrowBobForFrame(_animFrame),
					0);
			}
		}
	}

	// check if we got big explosions
	if (_explosionInFOV)
	{
		// big explosions cause the screen to flash as bright as possible before any explosions are actually drawn.
		// this causes everything to look like EGA for a single frame.
		// Meridian: no frikin flashing!!
		_flashScreen = false;
		if (_flashScreen)
		{
			for (int x = 0, y = 0; x < surface->getWidth() && y < surface->getHeight();)
			{
				Uint8 pixel = surface->getPixel(x, y);
				if (pixel)
				{
					pixel = (pixel & 0xF0) + 1; //avoid 0 pixel
					surface->setPixelIterative(&x, &y, pixel);
				}
			}
			_flashScreen = false;
		}
		else
		{
			for (const auto* explosion : _explosions)
			{
				_camera->convertVoxelToScreen(explosion->getPosition(), &bulletPositionScreen);
				if (explosion->isBig())
				{
					if (explosion->getCurrentFrame() >= 0)
					{
						tmpSurface = _game->getMod()->getSurfaceSet("X1.PCK")->getFrame(explosion->getCurrentFrame());
						Surface::blitRaw(surface, tmpSurface, bulletPositionScreen.x - (tmpSurface.getWidth() / 2), bulletPositionScreen.y - (tmpSurface.getHeight() / 2), 0, false, _nvColor);
					}
				}
				else if (explosion->isHit())
				{
					tmpSurface = _game->getMod()->getSurfaceSet("HIT.PCK")->getFrame(explosion->getCurrentFrame());
					Surface::blitRaw(surface, tmpSurface, bulletPositionScreen.x - 15, bulletPositionScreen.y - 25, 0, false, _nvColor);
				}
				else
				{
					tmpSurface = _game->getMod()->getSurfaceSet("SMOKE.PCK")->getFrame(explosion->getCurrentFrame());
					Surface::blitRaw(surface, tmpSurface, bulletPositionScreen.x - 15, bulletPositionScreen.y - 15, 0, false, _nvColor);
				}
			}
		}
	}

	surface->unlock();
}

/**
 * Handles mouse presses on the map.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::mousePress(Action *action, State *state)
{
	InteractiveSurface::mousePress(action, state);
	_camera->mousePress(action, state);
}

/**
 * Handles mouse releases on the map.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::mouseRelease(Action *action, State *state)
{
	InteractiveSurface::mouseRelease(action, state);
	_camera->mouseRelease(action, state);
}

/**
 * Handles keyboard presses on the map.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::keyboardPress(Action *action, State *state)
{
	InteractiveSurface::keyboardPress(action, state);
	_camera->keyboardPress(action, state);
}

/**
 * Handles map vision toggle mode.
 */

void Map::enableNightVision()
{
	_nightVisionOn = true;
	_debugVisionMode = 0;
	persistToggles();
}

void Map::toggleNightVision()
{
	_nightVisionOn = !_nightVisionOn;
	_debugVisionMode = 0;
	persistToggles();
}

void Map::toggleDebugVisionMode()
{
	_debugVisionMode = (_debugVisionMode + 1) % 3;
	_nightVisionOn = false;
	persistToggles();
}

void Map::persistToggles()
{
	if (Options::oxceToggleNightVisionType == 2)
	{
		// persisted per campaign
		_game->getSavedGame()->setToggleNightVision(_nightVisionOn);
	}
	else if (Options::oxceToggleNightVisionType == 1)
	{
		// persisted per battle
		_save->setToggleNightVision(_nightVisionOn);
	}

	if (Options::oxceToggleBrightnessType == 2)
	{
		// persisted per campaign
		_game->getSavedGame()->setToggleBrightness(_debugVisionMode);
	}
	else if (Options::oxceToggleBrightnessType == 1)
	{
		// persisted per battle
		_save->setToggleBrightness(_debugVisionMode);
	}
}

/**
 * Handles fade-in and fade-out shade modification
 * @param original tile/item/unit shade
 */

int Map::reShade(Tile *tile)
{
	// when modders just don't know where to stop...
	if (_debugVisionMode > 0)
	{
		if (_debugVisionMode == 1)
		{
			// Reaver's tests
			return tile->getShade() / 2;
		}
		// Meridian's debug helper
		return 0;
	}

	// no night vision
	if (_nvColor == 0)
	{
		return tile->getShade();
	}

	// already bright enough
	if ((tile->getShade() <= NIGHT_VISION_SHADE))
	{
		return tile->getShade();
	}

	// hybrid night vision (local)
	for (const auto* bu : *_save->getUnits())
	{
		if (bu->getFaction() == FACTION_PLAYER && !bu->isOut())
		{
			if (Position::distance2dSq(tile->getPosition(), bu->getPosition()) <= bu->getMaxViewDistanceAtDarkSquared())
			{
				return tile->getShade() > _fadeShade ? _fadeShade : tile->getShade();
			}
		}
	}

	// hybrid night vision (global)
	return std::min(+NIGHT_VISION_MAX_SHADE, tile->getShade());
}

/**
 * Handles keyboard releases on the map.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::keyboardRelease(Action *action, State *state)
{
	InteractiveSurface::keyboardRelease(action, state);
	_camera->keyboardRelease(action, state);
}

/**
 * Handles mouse over events on the map.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::mouseOver(Action *action, State *state)
{
	InteractiveSurface::mouseOver(action, state);
	_camera->mouseOver(action, state);
	_mouseX = (int)action->getAbsoluteXMouse();
	_mouseY = (int)action->getAbsoluteYMouse();
	setSelectorPosition(_mouseX, _mouseY);
}


/**
 * Sets the selector to a certain tile on the map.
 * @param mx mouse x position.
 * @param my mouse y position.
 */
void Map::setSelectorPosition(int mx, int my)
{
	int oldX = _selectorX, oldY = _selectorY;

	_camera->convertScreenToMap(mx, my + _spriteHeight/4, &_selectorX, &_selectorY);

	if (oldX != _selectorX || oldY != _selectorY)
	{
		_redraw = true;
	}
}

/**
 * Handles animating tiles. 8 Frames per animation.
 * @param redraw Redraw the battlescape?
 */
void Map::animate(bool redraw)
{
	_save->nextAnimFrame();
	_animFrame = _save->getAnimFrame();

	// random ambient sounds
	{
		if (!_save->getAmbienceRandom().empty())
		{
			_save->decreaseCurrentAmbienceDelay();
			if (_save->getCurrentAmbienceDelay() <= 0)
			{
				_save->resetCurrentAmbienceDelay();
				_save->playRandomAmbientSound();
			}
		}
	}

	// animate tiles
	for (int i = 0; i < _save->getMapSizeXYZ(); ++i)
	{
		_save->getTile(i)->animate();
	}

	// animate vapor
	for (auto i : Collections::rangeValueLess(_vaporParticles.size()))
	{
		auto& v = _vaporParticles[i];
		int posX = i % _camera->getMapSizeX();
		int posY = i / _camera->getMapSizeX();

		Collections::removeIf(
			v,
			[&](Particle& p)
			{
				if (p.animate())
				{
					Position tileOffset = p.updateScreenPosition();
					if (tileOffset != Position(0,0,0))
					{
						addVaporParticle(Position(posX,posY,0) + tileOffset, p);
						return true;
					}
					return false;
				}
				else
				{
					return true;
				}
			}
		);
	}

	// init vapor vector
	for (auto i : Collections::rangeValueLess(_vaporParticlesInit.size()))
	{
		auto& vi = _vaporParticlesInit[i];
		auto& vDest = _vaporParticles[i];
		if (vi.empty())
		{
			continue;
		}

		if (vDest.empty())
		{
			vi.swap(vDest);
		}
		else
		{
			vDest.insert(std::begin(vDest), std::begin(vi), std::end(vi));
		}


		Collections::removeAll(vi);
	}

	for (auto& tilePar : _vaporParticles)
	{
		if (tilePar.empty())
		{
			Collections::removeAll(tilePar);
		}
		else
		{
			std::sort(std::begin(tilePar), std::end(tilePar), [](const Particle& a, const Particle& b){ return a.getLayerZ() < b.getLayerZ(); });
		}
	}

	// animate certain units (large flying units have a propulsion animation)
	for (auto* bu : *_save->getUnits())
	{
		const Position pos = bu->getPosition();

		// skip units that do not have position
		if (pos == TileEngine::invalid)
		{
			continue;
		}

		if (_save->getDepth() > 0)
		{
			bu->setFloorAbove(false);

			// make sure this unit isn't obscured by the floor above him, otherwise it looks weird.
			if (_camera->getViewLevel() > pos.z)
			{
				for (int z = std::min(_camera->getViewLevel(), _save->getMapSizeZ() - 1); z != pos.z; --z)
				{
					if (!_save->getTile(Position(pos.x, pos.y, z))->hasNoFloor(0))
					{
						bu->setFloorAbove(true);
						break;
					}
				}
			}
		}

		bu->breathe();
	}

	if (redraw) _redraw = true;
}

/**
 * Draws the rectangle selector.
 * @param pos Pointer to a position.
 */
void Map::getSelectorPosition(Position *pos) const
{
	pos->x = _selectorX;
	pos->y = _selectorY;
	pos->z = _camera->getViewLevel();
}

/**
 * Calculates the offset of a soldier, when it is walking in the middle of 2 tiles.
 * @param unit Pointer to BattleUnit.
 * @param offset Pointer to the offset to return the calculation.
 */
UnitWalkingOffset Map::calculateWalkingOffset(const BattleUnit *unit) const
{
	UnitWalkingOffset result = { };

	int offsetX[8] = { 1, 1, 1, 0, -1, -1, -1, 0 };
	int offsetY[8] = { 1, 0, -1, -1, -1, 0, 1, 1 };
	int phase = unit->getWalkingPhase() + unit->getDiagonalWalkingPhase();
	int dir = unit->getDirection();
	int midphase = 4 + 4 * (dir % 2);
	int endphase = 8 + 8 * (dir % 2);
	int size = unit->getArmor()->getSize();

	result.ScreenOffset.x = 0;
	result.ScreenOffset.y = 0;

	if (size > 1)
	{
		if (dir < 1 || dir > 5)
			midphase = endphase;
		else if (dir == 5)
			midphase = 12;
		else if (dir == 1)
			midphase = 5;
		else
			midphase = 1;
	}
	if (unit->getVerticalDirection())
	{
		midphase = 4;
		endphase = 8;
	}
	else if ((unit->getStatus() == STATUS_WALKING || unit->getStatus() == STATUS_FLYING))
	{
		if (phase < midphase)
		{
			result.ScreenOffset.x = phase * 2 * offsetX[dir];
			result.ScreenOffset.y = - phase * offsetY[dir];
		}
		else
		{
			result.ScreenOffset.x = (phase - endphase) * 2 * offsetX[dir];
			result.ScreenOffset.y = - (phase - endphase) * offsetY[dir];
		}
	}

	result.NormalizedMovePhase = endphase == 16 ? phase : phase * 2;

	// If we are walking in between tiles, interpolate it's terrain level.
	if (unit->getStatus() == STATUS_WALKING || unit->getStatus() == STATUS_FLYING)
	{
		const auto posCurr = unit->getPosition();
		const auto posDest = unit->getDestination();
		const auto posLast = unit->getLastPosition();
		if (phase < midphase)
		{
			int fromLevel = getTerrainLevel(posCurr, size);
			int toLevel = getTerrainLevel(posDest, size);
			if (posCurr.z > posDest.z)
			{
				// going down a level, so toLevel 0 becomes +24, -8 becomes  16
				toLevel += Position::TileZ*(posCurr.z - posDest.z);
			}
			else if (posCurr.z < posDest.z)
			{
				// going up a level, so toLevel 0 becomes -24, -8 becomes -16
				toLevel = -Position::TileZ*(posDest.z - posCurr.z) + abs(toLevel);
			}
			result.TerrainLevelOffset = Interpolate(fromLevel, toLevel, phase, endphase);
		}
		else
		{
			// from phase 4 onwards the unit behind the scenes already is on the destination tile
			// we have to get it's last position to calculate the correct offset
			int fromLevel = getTerrainLevel(posLast, size);
			int toLevel = getTerrainLevel(posDest, size);
			if (posLast.z > posDest.z)
			{
				// going down a level, so fromLevel 0 becomes -24, -8 becomes -32
				fromLevel -= Position::TileZ*(posLast.z - posDest.z);
			}
			else if (posLast.z < posDest.z)
			{
				// going up a level, so fromLevel 0 becomes +24, -8 becomes 16
				fromLevel = Position::TileZ*(posDest.z - posLast.z) - abs(fromLevel);
			}
			result.TerrainLevelOffset = Interpolate(fromLevel, toLevel, phase, endphase);
		}
	}
	else
	{
		result.TerrainLevelOffset = getTerrainLevel(unit->getPosition(), size);
	}
	result.ScreenOffset.y += result.TerrainLevelOffset;
	return result;
}


/**
  * Terrainlevel goes from 0 to -24. For a larger sized unit, we need to pick the highest terrain level, which is the lowest number...
  * @param pos Position.
  * @param size Size of the unit we want to get the level from.
  * @return terrainlevel.
  */
int Map::getTerrainLevel(const Position& pos, int size) const
{
	int lowestlevel = 0;

	for (int x = 0; x < size; x++)
	{
		for (int y = 0; y < size; y++)
		{
			int l = _save->getTile(pos + Position(x,y,0))->getTerrainLevel();
			if (l < lowestlevel)
				lowestlevel = l;
		}
	}

	return lowestlevel;
}

/**
 * Sets the 3D cursor to selection/aim mode.
 * @param type Cursor type.
 * @param size Size of cursor.
 */
void Map::setCursorType(CursorType type, int size)
{
	// reset cursor indicator cache
	_cacheActiveWeaponUfopediaArticleUnlocked = -1;
	_cacheIsCtrlPressed = false;
	_cacheCursorPosition = TileEngine::invalid;
	_cacheHasLOS = -1;

	_cursorType = type;
	if (_cursorType == CT_NORMAL)
		_cursorSize = size;
	else
		_cursorSize = 1;
}

/**
 * Gets the cursor type.
 * @return cursor type.
 */
CursorType Map::getCursorType() const
{
	return _cursorType;
}

/**
 * Puts a projectile sprite on the map.
 * @param projectile Projectile to place.
 */
void Map::setProjectile(Projectile *projectile)
{
	_projectile = projectile;
	if (projectile && Options::battleSmoothCamera)
	{
		_launch = true;
	}
}

/**
 * Gets the current projectile sprite on the map.
 * @return Projectile or 0 if there is no projectile sprite on the map.
 */
Projectile *Map::getProjectile() const
{
	return _projectile;
}

/**
 * Add new vapor particle.
 * @param pos Tile position of particle.
 * @param particle Particle to add.
 */
void Map::addVaporParticle(Position pos, Particle particle)
{
	if ((int)(_transparencies->size()) < (particle.getColor() + 1) * Mod::TransparenciesOpacityLevels * Mod::TransparenciesPaletteColors)
	{
		return;
	}
	if (pos.x >= _camera->getMapSizeX() || pos.y >= _camera->getMapSizeY())
	{
		return;
	}
	if (pos.x < 0 || pos.y < 0)
	{
		return;
	}

	auto& v = _vaporParticlesInit[_camera->getMapSizeX() * pos.y + pos.x];

	// as there will usually be more than one Particle, we prepare more space
	if (v.capacity() < 64)
	{
		v.reserve(64);
	}

	v.push_back(particle);
}

/**
 * Get all vapor for tile.
 * @param tile current tile.
 * @param topLayer if tile is top visible layer, if true then will return particles belongs to upper tiles.
 * @return range of particles that should be drawn.
 */
Collections::Range<const Particle*> Map::getVaporParticle(const Tile* tile, int topLayer) const
{
	auto pos = tile->getPosition();
	auto& v = _vaporParticles[_camera->getMapSizeX() * pos.y + pos.x];
	int startZ = pos.z * Particle::LayerAccuracy + (topLayer & 1);
	int endZ = startZ + Particle::LayerAccuracy / 2;
	auto* s = std::partition_point(v.data(), v.data() + v.size(), [&](const Particle& a){ return a.getLayerZ() < startZ; });
	auto* e = (topLayer & 2) ? v.data() + v.size() : std::partition_point(s, v.data() + v.size(), [&](const Particle& a){ return a.getLayerZ() < endZ; });
	return Collections::Range{ s, e };
}

/**
 * Gets a list of explosion sprites on the map.
 * @return A list of explosion sprites.
 */
std::list<Explosion*> *Map::getExplosions()
{
	return &_explosions;
}

/**
 * Gets the pointer to the camera.
 * @return Pointer to camera.
 */
Camera *Map::getCamera()
{
	return _camera;
}

/**
 * Timers only work on surfaces so we have to pass this on to the camera object.
 */
void Map::scrollMouse()
{
	_camera->scrollMouse();
}

/**
 * Timers only work on surfaces so we have to pass this on to the camera object.
 */
void Map::scrollKey()
{
	_camera->scrollKey();
}

/**
 * Modify the fade shade level if fade's in progress.
 */
void Map::fadeShade()
{
	bool hold = SDL_GetKeyState(NULL)[Options::keyNightVisionHold];
	if ((_nightVisionOn && !hold) || (!_nightVisionOn && hold))
	{
		_nvColor = Options::oxceNightVisionColor;
		if (_fadeShade > NIGHT_VISION_SHADE) // 0 = max brightness
		{
			--_fadeShade;
		}
	}
	else
	{
		if (_nvColor != 0)
		{
			if (_fadeShade < _save->getGlobalShade())
			{
				// gradually fade away
				++_fadeShade;
			}
			else
			{
				// and at the end turn off night vision
				_nvColor = 0;
			}
		}
	}
}

/**
 * Gets a list of waypoints on the map.
 * @return A list of waypoints.
 */
std::vector<Position> *Map::getWaypoints()
{
	return &_waypoints;
}

/**
 * Sets mouse-buttons' pressed state.
 * @param button Index of the button.
 * @param pressed The state of the button.
 */
void Map::setButtonsPressed(Uint8 button, bool pressed)
{
	setButtonPressed(button, pressed);
}

/**
 * Sets the unitDying flag.
 * @param flag True if the unit is dying.
 */
void Map::setUnitDying(bool flag)
{
	_unitDying = flag;
}

/**
 * Updates the selector to the last-known mouse position.
 */
void Map::refreshSelectorPosition()
{
	setSelectorPosition(_mouseX, _mouseY);
}

/**
 * Special handling for setting the height of the map viewport.
 * @param height the new base screen height.
 */
void Map::setHeight(int height)
{
	Surface::setHeight(height);
	_visibleMapHeight = height - _iconHeight;
	_message->setHeight((_visibleMapHeight < 200)? _visibleMapHeight : 200);
	_message->setY((_visibleMapHeight - _message->getHeight()) / 2);
}

/**
 * Special handling for setting the width of the map viewport.
 * @param width the new base screen width.
 */
void Map::setWidth(int width)
{
	int dX = width - getWidth();
	Surface::setWidth(width);
	_message->setX(_message->getX() + dX / 2);
}

/**
 * Get the hidden movement screen's vertical position.
 * @return the vertical position of the hidden movement window.
 */
int Map::getMessageY() const
{
	return _message->getY();
}

/**
 * Get the icon height.
 */
int Map::getIconHeight() const
{
	return _iconHeight;
}

/**
 * Get the icon width.
 */
int Map::getIconWidth() const
{
	return _iconWidth;
}

/**
 * Returns the angle(left/right balance) of a sound effect,
 * based off a map position.
 * @param pos the map position to calculate the sound angle from.
 * @return the angle of the sound (280 to 440).
 */
int Map::getSoundAngle(const Position& pos) const
{
	int midPoint = getWidth() / 2;
	Position relativePosition;

	_camera->convertMapToScreen(pos, &relativePosition);
	// cap the position to the screen edges relative to the center,
	// negative values indicating a left-shift, and positive values shifting to the right.
	relativePosition.x = Clamp((relativePosition.x + _camera->getMapOffset().x) - midPoint, -midPoint, midPoint);

	// convert the relative distance to a relative increment of an 80 degree angle
	// we use +- 80 instead of +- 90, so as not to go ALL the way left or right
	// which would effectively mute the sound out of one speaker.
	// since Mix_SetPosition uses modulo 360, we can't feed it a negative number, so add 360 instead.
	return 360 + (relativePosition.x / (midPoint / 80.0));
}

/**
 * Reset the camera smoothing bool.
 */
void Map::resetCameraSmoothing()
{
	_smoothingEngaged = false;
}

/**
 * Set the "explosion flash" bool.
 * @param flash should the screen be rendered in EGA this frame?
 */
void Map::setBlastFlash(bool flash)
{
	_flashScreen = flash;
}

/**
 * Checks if the screen is still being rendered in EGA.
 * @return if we are still in EGA mode.
 */
bool Map::getBlastFlash() const
{
	return _flashScreen;
}

/**
 * Resets obstacle markers.
 */
void Map::resetObstacles(void)
{
	for (int z = 0; z < _save->getMapSizeZ(); z++)
		for (int y = 0; y < _save->getMapSizeY(); y++)
			for (int x = 0; x < _save->getMapSizeX(); x++)
			{
				Tile *tile = _save->getTile(Position(x, y, z));
				if (tile) tile->resetObstacle();
			}
	_showObstacles = false;
}

/**
 * Enables obstacle markers.
 */
void Map::enableObstacles(void)
{
	_showObstacles = true;
	if (_obstacleTimer)
	{
		_obstacleTimer->stop();
		_obstacleTimer->start();
	}
}

/**
 * Disables obstacle markers.
 */
void Map::disableObstacles(void)
{
	_showObstacles = false;
	if (_obstacleTimer)
	{
		_obstacleTimer->stop();
	}
}

}
