#include "input.h"

#include <algorithm>

namespace {
size_t next(const std::string &s, size_t p) {
    if (p >= s.size()) return s.size();
    unsigned char c = (unsigned char)s[p];
    size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    return std::min(s.size(), p + n);
}
size_t prev(const std::string &s, size_t p) {
    if (!p) return 0;
    --p;
    while (p && ((unsigned char)s[p] & 0xC0) == 0x80) --p;
    return p;
}
}

TextField::TextField(size_t max_bytes) : max_bytes_(max_bytes) {}
void TextField::set_text(const std::string &text) {
    value_.clear(); caret_ = 0;
    for (size_t p = 0; p < text.size();) {
        size_t q = next(text, p);
        if (q - p > max_bytes_ - value_.size()) break;
        value_.append(text, p, q - p); p = q;
    }
    caret_ = value_.size();
}
bool TextField::insert(unsigned cp) {
    std::string b;
    if (cp < 0x80) b += (char)cp;
    else if (cp < 0x800) { b += (char)(0xC0 | (cp >> 6)); b += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { b += (char)(0xE0 | (cp >> 12)); b += (char)(0x80 | ((cp >> 6) & 63)); b += (char)(0x80 | (cp & 63)); }
    else { b += (char)(0xF0 | (cp >> 18)); b += (char)(0x80 | ((cp >> 12) & 63)); b += (char)(0x80 | ((cp >> 6) & 63)); b += (char)(0x80 | (cp & 63)); }
    if (b.size() > max_bytes_ - value_.size()) return false;
    value_.insert(caret_, b); caret_ += b.size(); return true;
}
void TextField::backspace() { if (caret_) { size_t p = prev(value_, caret_); value_.erase(p, caret_ - p); caret_ = p; } }
void TextField::erase_forward() { if (caret_ < value_.size()) value_.erase(caret_, next(value_, caret_) - caret_); }
void TextField::left() { caret_ = prev(value_, caret_); }
void TextField::right() { caret_ = next(value_, caret_); }
void TextField::home() { caret_ = 0; }
void TextField::end() { caret_ = value_.size(); }
void TextField::clear() { value_.clear(); caret_ = 0; }

#include "graphite.h"
#include <vector>

namespace {
struct RowKey { unsigned cp; };
const unsigned ru_rows[3][11] = {
    {0x0439,0x0446,0x0443,0x043a,0x0435,0x043d,0x0433,0x0448,0x0449,0x0437,0x0445},
    {0x0444,0x044b,0x0432,0x0430,0x043f,0x0440,0x043e,0x043b,0x0434,0x0436,0x044d},
    {0x044f,0x0447,0x0441,0x043c,0x0438,0x0442,0x044c,0x0431,0x044e,0,0}
};
const char *const en_rows[3] = {"qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};
const char *const num_rows[3] = {"1234567890", "@#$%&*()-_", "+={}[]:;/?"};
int row_count(int row, int language, int layer) {
    if (row == 0 || row == 4) return 6;
    if (layer) return 10;
    if (language) return 10;
    return row == 1 ? 11 : row == 2 ? 10 : 9;
}
unsigned row_codepoint(int row, int i, int language, int layer) {
    if (layer) return (unsigned char)num_rows[row-1][i];
    if (language) return (unsigned char)en_rows[row-1][i];
    return ru_rows[row-1][i];
}
int row_y(int row) { return 514 + row * 56; }
std::string utf8(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
    return s;
}
}
KbModel::KbModel() = default;
InputEvent KbModel::hit(int x, int y) {
    if (y < 512 || y >= 800) return {};
    int row = (y - 514) / 56;
    if (row < 0 || row > 4 || y >= row_y(row) + 50) return {};
    int count = row_count(row, language_, layer_);
    for (int i=0;i<count;i++) {
        // Hit area is the full cell; the 2 px visual gutter is not dead space.
        int left = row == 4 ? (i * 480 / count) : (i * 480 / count + 2);
        int right = row == 4 ? ((i + 1) * 480 / count) : ((i + 1) * 480 / count - 2);
        if (x < left || x >= right) continue;
        if (row == 0) {
            if (i == 0) { language_ = 1-language_; return {InputAction::Language,0}; }
            if (i == 1) { layer_ = 1-layer_; return {InputAction::Layer,0}; }
            if (i == 2) { shift_ = true; return {InputAction::Shift,0}; }
            if (i == 3) return {InputAction::Home,0};
            if (i == 4) return {InputAction::End,0};
            return {InputAction::Cancel,0};
        }
        if (row == 4) {
            // Keep the 5 visible bottom controls wide enough for touch.
            int part = x * 5 / 480;
            static const InputAction actions[5] = {InputAction::Left,InputAction::Backspace,InputAction::Space,InputAction::Enter,InputAction::Cancel};
            return {actions[part],0};
        }
        unsigned cp = row_codepoint(row,i,language_,layer_);
        if (shift_ && cp >= 'a' && cp <= 'z') cp -= 32;
        if (shift_ && cp >= 0x0430 && cp <= 0x044f) cp -= 0x20;
        shift_ = false;
        return {InputAction::Character,cp};
    }
    return {};
}
void KbModel::draw(Gfx &g) const {
    g.rect(0,512,480,288,theme.surf);
    const char *top[6]={language_?"EN":"РУ",layer_?"ABC":"123","SHIFT","HOME","END","BACK"};
    for (int row=0;row<5;row++) {
        int n=row == 4 ? 5 : row_count(row,language_,layer_), y=row_y(row);
        for(int i=0;i<n;i++) {
            int x=i*480/n, w=(i+1)*480/n-x;
            g.rect(x+2,y,w-4,50,theme.surf2); g.frame(x+2,y,w-4,50,theme.line2);
            std::string label;
            if(row==0) label=top[i];
            else if(row==4) {
                static const char *wide[5]={"←","⌫","ПРОБЕЛ","OK","ОТМЕНА"};
                label=wide[i];
            }
            else label=utf8(row_codepoint(row,i,language_,layer_));
            g.text(x+w/2,y+18,label,11,theme.fg,TF_CENTER);
        }
    }
}
