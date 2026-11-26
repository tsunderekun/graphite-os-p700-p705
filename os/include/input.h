#pragma once

#include <cstddef>
#include <string>

struct Gfx;

class TextField {
public:
    explicit TextField(size_t max_bytes = 240);
    void set_text(const std::string &text);
    const std::string &text() const { return value_; }
    size_t caret() const { return caret_; }
    bool insert(unsigned cp);
    void backspace();
    void erase_forward();
    void left();
    void right();
    void home();
    void end();
    void clear();
private:
    std::string value_;
    size_t caret_ = 0;
    size_t max_bytes_;
};

enum class InputAction { NoneAction, Character, Space, Backspace, DeleteForward, Left, Right, Home, End, Enter, Shift, Layer, Language, Cancel };
struct InputEvent { InputAction action = InputAction::NoneAction; unsigned codepoint = 0; };

class KbModel {
public:
    KbModel();
    InputEvent hit(int x, int y);
    void draw(Gfx &g) const;
private:
    int language_ = 0; // RU / EN
    int layer_ = 0; // letters / numbers
    bool shift_ = false;
};
