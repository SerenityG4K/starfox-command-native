// Couche plateforme : fenêtre, contexte OpenGL et entrées.
//
// L'interface est volontairement indépendante de Win32 pour qu'un backend SDL2
// puisse la remplacer sans toucher au reste du runtime (portage Linux/macOS).
#pragma once

#include <array>
#include <string>

namespace sfc::platform {

enum class Key {
    Escape, Space, Tab, Shift, Ctrl,
    Left, Right, Up, Down,
    W, A, S, D, Q, E, R, F, V, P, Z,
    PageUp, PageDown,
    Count,
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(Key::Count)> down{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> pressed{};   // front montant
    float mouseX = 0.0f, mouseY = 0.0f;
    float mouseDeltaX = 0.0f, mouseDeltaY = 0.0f;
    float wheelDelta = 0.0f;
    bool mouseLeft = false;
    bool mouseRight = false;

    bool isDown(Key key) const { return down[static_cast<std::size_t>(key)]; }
    bool wasPressed(Key key) const { return pressed[static_cast<std::size_t>(key)]; }

    // Chaque touche par son code natif (le code virtuel sous Windows, de 1 à 255) : de quoi lier
    // n'importe laquelle à une commande du jeu, là où Key ne nomme que quelques touches.
    std::array<bool, 256> nativeDown{};
    std::array<bool, 256> nativePressed{};   // front montant
    bool isNativeDown(int code) const { return code > 0 && code < 256 && nativeDown[code]; }
    bool wasNativePressed(int code) const { return code > 0 && code < 256 && nativePressed[code]; }
};

class Window {
public:
    // Détails propres au backend. Publique parce que la procédure de fenêtre
    // Win32, qui est une fonction libre imposée par l'API, doit y accéder.
    struct Impl;

    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Crée la fenêtre et un contexte OpenGL 3.3 core.
    bool create(const std::string& title, int width, int height);
    void destroy();

    // Traite les messages en attente ; renvoie false quand on doit quitter.
    bool pumpEvents();
    void swapBuffers();

    // Synchronisation verticale. La désactiver est ce qui laisse le rendu
    // monter au-delà de 60 images par seconde.
    void setVSync(bool enabled);
    bool vsync() const { return vsync_; }

    void setTitle(const std::string& title);

    int width() const { return width_; }
    int height() const { return height_; }
    const InputState& input() const { return input_; }

private:
    Impl* impl_ = nullptr;
    InputState input_;
    int width_ = 0;
    int height_ = 0;
    bool vsync_ = false;
};

}  // namespace sfc::platform
