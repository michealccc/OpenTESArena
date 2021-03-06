#include <vector>

#include "SDL.h"

#include "CinematicPanel.h"
#include "CursorAlignment.h"
#include "ImageSequencePanel.h"
#include "ImagePanel.h"
#include "MainMenuPanel.h"
#include "Panel.h"
#include "RichTextString.h"
#include "Surface.h"
#include "TextAlignment.h"
#include "TextBox.h"
#include "../Game/Game.h"
#include "../Game/Options.h"
#include "../Math/Rect.h"
#include "../Math/Vector2.h"
#include "../Media/Color.h"
#include "../Media/FontLibrary.h"
#include "../Media/PaletteFile.h"
#include "../Media/PaletteName.h"
#include "../Media/PaletteUtils.h"
#include "../Media/TextureFile.h"
#include "../Media/TextureName.h"
#include "../Media/TextureSequenceName.h"
#include "../Rendering/Renderer.h"

#include "components/vfs/manager.hpp"

const Panel::CursorData Panel::CursorData::EMPTY(nullptr, CursorAlignment::TopLeft);

Panel::CursorData::CursorData(const Texture *texture, CursorAlignment alignment)
{
	this->texture = texture;
	this->alignment = alignment;
}

const Texture *Panel::CursorData::getTexture() const
{
	return this->texture;
}

CursorAlignment Panel::CursorData::getAlignment() const
{
	return this->alignment;
}

Panel::Panel(Game &game)
	: game(game) { }

Texture Panel::createTooltip(const std::string &text,
	FontName fontName, FontLibrary &fontLibrary, Renderer &renderer)
{
	const Color textColor(255, 255, 255, 255);
	const Color backColor(32, 32, 32, 192);

	const int x = 0;
	const int y = 0;

	const RichTextString richText(
		text,
		fontName,
		textColor,
		TextAlignment::Left,
		fontLibrary);

	// Create text.
	const TextBox textBox(x, y, richText, fontLibrary, renderer);
	const Surface &textSurface = textBox.getSurface();

	// Create background. Make it a little bigger than the text box.
	constexpr int padding = 4;
	Surface background = Surface::createWithFormat(
		textSurface.getWidth() + padding, textSurface.getHeight() + padding,
		Renderer::DEFAULT_BPP, Renderer::DEFAULT_PIXELFORMAT);
	background.fill(backColor.r, backColor.g, backColor.b, backColor.a);

	// Offset the text from the top left corner by a bit so it isn't against the side 
	// of the tooltip (for aesthetic purposes).
	SDL_Rect rect;
	rect.x = padding / 2;
	rect.y = padding / 2;
	rect.w = textSurface.getWidth();
	rect.h = textSurface.getHeight();

	// Draw the text onto the background.
	SDL_BlitSurface(textSurface.get(), nullptr, background.get(), &rect);

	// Create a hardware texture for the tooltip.
	Texture tooltip = renderer.createTextureFromSurface(background);

	return tooltip;
}

std::unique_ptr<Panel> Panel::defaultPanel(Game &game)
{
	// If not showing the intro, then jump to the main menu.
	if (!game.getOptions().getMisc_ShowIntro())
	{
		return std::make_unique<MainMenuPanel>(game);
	}

	// All of these lambdas are linked together like a stack by each panel's last
	// argument.
	auto changeToMainMenu = [](Game &game)
	{
		game.setPanel<MainMenuPanel>(game);
	};

	auto changeToIntroStory = [changeToMainMenu](Game &game)
	{
		std::vector<std::string> paletteNames
		{
			"SCROLL03.IMG", "SCROLL03.IMG", "SCROLL03.IMG"
		};

		std::vector<std::string> textureNames
		{
			"SCROLL01.IMG", "SCROLL02.IMG", "SCROLL03.IMG"
		};

		// In the original game, the last frame ("...hope flies on death's wings...")
		// seems to be a bit shorter.
		std::vector<double> imageDurations
		{
			13.0, 13.0, 10.0
		};

		game.setPanel<ImageSequencePanel>(game, paletteNames, textureNames,
			imageDurations, changeToMainMenu);
	};

	auto changeToScrolling = [changeToIntroStory](Game &game)
	{
		game.setPanel<CinematicPanel>(
			game,
			PaletteFile::fromName(PaletteName::Default),
			TextureFile::fromName(TextureSequenceName::OpeningScroll),
			0.042,
			changeToIntroStory);
	};

	auto changeToQuote = [changeToScrolling](Game &game)
	{
		const double secondsToDisplay = 5.0;
		const std::string &textureName = TextureFile::fromName(TextureName::IntroQuote);
		const std::string &paletteName = textureName;
		game.setPanel<ImagePanel>(game, paletteName, textureName,
			secondsToDisplay, changeToScrolling);
	};

	auto makeIntroTitlePanel = [changeToQuote, &game]()
	{
		const double secondsToDisplay = 5.0;
		const std::string &textureName = TextureFile::fromName(TextureName::IntroTitle);
		const std::string &paletteName = textureName;
		return std::make_unique<ImagePanel>(game, paletteName, textureName,
			secondsToDisplay, changeToQuote);
	};

	// Decide how the game starts up. If only the floppy disk data is available,
	// then go to the splash screen. Otherwise, load the intro book video.
	const auto &exeData = game.getBinaryAssetLibrary().getExeData();
	const bool isFloppyVersion = exeData.isFloppyVersion();
	if (!isFloppyVersion)
	{
		auto changeToTitle = [makeIntroTitlePanel](Game &game)
		{
			game.setPanel(makeIntroTitlePanel());
		};

		auto makeIntroBookPanel = [changeToTitle, &game]()
		{
			return std::make_unique<CinematicPanel>(
				game,
				PaletteFile::fromName(PaletteName::Default),
				TextureFile::fromName(TextureSequenceName::IntroBook),
				1.0 / 7.0,
				changeToTitle);
		};

		return makeIntroBookPanel();
	}
	else
	{
		return makeIntroTitlePanel();
	}
}

Panel::CursorData Panel::getCurrentCursor() const
{
	// Null by default.
	return CursorData(nullptr, CursorAlignment::TopLeft);
}

void Panel::handleEvent(const SDL_Event &e)
{
	// Do nothing by default.
	static_cast<void>(e);
}

void Panel::onPauseChanged(bool paused)
{
	// Do nothing by default.
	static_cast<void>(paused);
}

void Panel::resize(int windowWidth, int windowHeight)
{
	// Do nothing by default.
	static_cast<void>(windowWidth);
	static_cast<void>(windowHeight);
}

Game &Panel::getGame() const
{
	return this->game;
}

Panel::CursorData Panel::getDefaultCursor() const
{
	auto &game = this->getGame();
	auto &renderer = game.getRenderer();
	auto &textureManager = game.getTextureManager();

	const std::string &paletteFilename = PaletteFile::fromName(PaletteName::Default);
	PaletteID paletteID;
	if (!textureManager.tryGetPaletteID(paletteFilename.c_str(), &paletteID))
	{
		DebugLogWarning("Couldn't get palette ID for \"" + paletteFilename + "\".");
		return CursorData::EMPTY;
	}

	const std::string &textureFilename = TextureFile::fromName(TextureName::SwordCursor);
	TextureID textureID;
	if (!textureManager.tryGetTextureID(textureFilename.c_str(), paletteID, renderer, &textureID))
	{
		DebugLogWarning("Couldn't get texture ID for \"" + textureFilename + "\".");
		return CursorData::EMPTY;
	}

	const Texture &texture = textureManager.getTextureHandle(textureID);
	return CursorData(&texture, CursorAlignment::TopLeft);
}

TextureID Panel::getTextureID(const std::string &textureName,
	const std::string &paletteName) const
{
	auto &textureManager = game.getTextureManager();
	auto &renderer = game.getRenderer();

	const std::string &paletteFilename =
		PaletteUtils::isBuiltIn(paletteName) ? textureName : paletteName;

	PaletteID paletteID;
	if (!textureManager.tryGetPaletteID(paletteFilename.c_str(), &paletteID))
	{
		DebugCrash("Couldn't get palette ID for \"" + paletteFilename + "\".");
	}

	TextureID textureID;
	if (!textureManager.tryGetTextureID(textureName.c_str(), paletteID, renderer, &textureID))
	{
		DebugCrash("Couldn't get texture ID for \"" + textureName + "\".");
	}

	return textureID;
}

TextureID Panel::getTextureID(TextureName textureName, PaletteName paletteName) const
{
	const std::string &textureFilename = TextureFile::fromName(textureName);
	const std::string &paletteFilename = PaletteFile::fromName(paletteName);
	return this->getTextureID(textureFilename, paletteFilename);
}

TextureUtils::TextureIdGroup Panel::getTextureIDs(const std::string &textureName,
	const std::string &paletteName) const
{
	auto &textureManager = game.getTextureManager();
	auto &renderer = game.getRenderer();

	const std::string &paletteFilename =
		PaletteUtils::isBuiltIn(paletteName) ? textureName : paletteName;

	PaletteID paletteID;
	if (!textureManager.tryGetPaletteID(paletteFilename.c_str(), &paletteID))
	{
		DebugCrash("Couldn't get palette ID for \"" + paletteFilename + "\".");
	}

	TextureUtils::TextureIdGroup textureIDs;
	if (!textureManager.tryGetTextureIDs(textureName.c_str(), paletteID, renderer, &textureIDs))
	{
		DebugCrash("Couldn't get texture IDs for \"" + textureName + "\".");
	}

	return textureIDs;
}

TextureUtils::TextureIdGroup Panel::getTextureIDs(TextureName textureName,
	PaletteName paletteName) const
{
	const std::string &textureFilename = TextureFile::fromName(textureName);
	const std::string &paletteFilename = PaletteFile::fromName(paletteName);
	return this->getTextureIDs(textureFilename, paletteFilename);
}

void Panel::tick(double dt)
{
	// Do nothing by default.
	static_cast<void>(dt);
}

void Panel::renderSecondary(Renderer &renderer)
{
	// Do nothing by default.
	static_cast<void>(renderer);
}
