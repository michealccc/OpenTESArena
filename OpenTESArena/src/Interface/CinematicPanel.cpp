#include "SDL.h"

#include "CinematicPanel.h"
#include "Texture.h"
#include "../Game/Game.h"
#include "../Media/TextureManager.h"
#include "../Rendering/Renderer.h"

CinematicPanel::CinematicPanel(Game &game,
	const std::string &paletteName, const std::string &sequenceName,
	double secondsPerImage, const std::function<void(Game&)> &endingAction)
	: Panel(game), paletteName(paletteName), sequenceName(sequenceName)
{
	this->skipButton = [&endingAction]()
	{
		return Button<Game&>(endingAction);
	}();

	this->secondsPerImage = secondsPerImage;
	this->currentSeconds = 0.0;
	this->imageIndex = 0;
}

void CinematicPanel::handleEvent(const SDL_Event &e)
{
	const auto &inputManager = this->getGame().getInputManager();
	bool leftClick = inputManager.mouseButtonPressed(e, SDL_BUTTON_LEFT);
	bool spacePressed = inputManager.keyPressed(e, SDLK_SPACE);
	bool enterPressed = inputManager.keyPressed(e, SDLK_RETURN) ||
		inputManager.keyPressed(e, SDLK_KP_ENTER);
	bool escapePressed = inputManager.keyPressed(e, SDLK_ESCAPE);

	bool skipHotkeyPressed = spacePressed || enterPressed || escapePressed;

	if (leftClick || skipHotkeyPressed)
	{
		this->skipButton.click(this->getGame());
	}
}

void CinematicPanel::tick(double dt)
{
	// See if it's time for the next image.
	this->currentSeconds += dt;
	while (this->currentSeconds > this->secondsPerImage)
	{
		this->currentSeconds -= this->secondsPerImage;
		this->imageIndex++;
	}

	auto &game = this->getGame();
	auto &renderer = game.getRenderer();
	auto &textureManager = game.getTextureManager();
	const TextureUtils::TextureIdGroup textureIDs =
		this->getTextureIDs(this->sequenceName, this->paletteName);

	// If at the end, then prepare for the next panel.
	if (this->imageIndex >= textureIDs.getCount())
	{
		this->imageIndex = static_cast<int>(textureIDs.getCount() - 1);
		this->skipButton.click(game);
	}
}

void CinematicPanel::render(Renderer &renderer)
{
	// Clear full screen.
	renderer.clear();

	// Get texture IDs in advance of any texture references.
	const TextureUtils::TextureIdGroup textureIDs =
		this->getTextureIDs(this->sequenceName, this->paletteName);
	const TextureID textureID = textureIDs.getID(this->imageIndex);

	// Draw current frame of the cinematic.
	const auto &textureManager = this->getGame().getTextureManager();
	const TextureRef texture = textureManager.getTextureRef(textureID);
	renderer.drawOriginal(texture.get());
}
