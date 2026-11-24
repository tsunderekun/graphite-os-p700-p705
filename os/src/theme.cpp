#include "graphite.h"
#include <cstring>

struct ThemeDef {
    const char *id, *name, *code;
    int family;
    const char *bg, *bar, *surf, *surf2, *line, *line2, *art;
    const char *fg, *dim, *accent, *accent2, *track, *sel, *num, *note, *onacc;
};

static const ThemeDef defs[] = {
    {"graphite", "Графит", "GRAPHITE", FAM_DARK,
     "#101419", "#081010", "#192021", "#3a414a", "#3a454a", "#212429", "#080c08",
     "#efebef", "#8c969c", "#3acad6", "#29b6c5", "#424d52", "#192021", "#7b828c",
     "#5a656b", "#081010"},
    {"ember", "Уголь", "EMBER", FAM_DARK,
     "#101410", "#100c08", "#211c19", "#423929", "#4a4131", "#292419", "#080808",
     "#f7ebe6", "#9c9284", "#e6a252", "#ce8a3a", "#4a4131", "#211c10", "#8c8273",
     "#6b5d4a", "#101008"},
    {"slate", "Сланец", "SLATE", FAM_DARK,
     "#191c21", "#101419", "#212429", "#3a454a", "#3a414a", "#212831", "#101010",
     "#efefef", "#949aa5", "#3acad6", "#29b6c5", "#424d52", "#192429", "#7b868c",
     "#636973", "#081010"},
    {"paper", "Бумага", "PAPER", FAM_LIGHT,
     "#f7ebde", "#e6dfce", "#fff7ef", "#e6d7c5", "#ceba9c", "#e6dbc5", "#e6dfce",
     "#211c10", "#63594a", "#087173", "#085d63", "#cebea5", "#efdfce", "#6b6152",
     "#948a6b", "#f7f3e6"},
    {"paper-n", "Бумага · нейтр.", "PAPER-N", FAM_LIGHT,
     "#efeff7", "#e6e7e6", "#fffbff", "#dedfde", "#bdc2c5", "#dee3e6", "#e6ebef",
     "#101819", "#5a5d63", "#087173", "#085d63", "#c5c6c5", "#e6ebef", "#63696b",
     "#8c9694", "#f7f3e6"},
    {"book", "Книга", "BOOK", FAM_GREY,
     "#cec2b5", "#bdb6a5", "#d6cebd", "#b5aa9c", "#9c9284", "#b5ae9c", "#bdb6a5",
     "#191810", "#4a4131", "#085152", "#004142", "#ada294", "#ded2c5", "#524d42",
     "#7b7163", "#f7efe6"},
    {"book-n", "Книга · нейтр.", "BOOK-N", FAM_GREY,
     "#c5c6c5", "#b5babd", "#ced2d6", "#adaead", "#949a9c", "#adb2b5", "#b5babd",
     "#191c19", "#42494a", "#085152", "#004142", "#a5a6a5", "#d6d7d6", "#4a5152",
     "#73757b", "#f7efe6"},
};

struct AccentDef {
    const char *id, *name;
    const char *dark[3], *ink[3], *deep[3];
};

static const AccentDef accents[] = {
    {"turquoise", "Бирюза", {"#3fc8d0", "#2fb6c0", "#0f1114"},
                            {"#0b7076", "#085c61", "#f7f2e7"},
                            {"#085357", "#064246", "#f2ece0"}},
    {"amber", "Янтарь", {"#e0a052", "#c98b3f", "#171008"},
                        {"#8a5a12", "#6f470c", "#fbf4e6"},
                        {"#67430d", "#52360a", "#f2ece0"}},
    {"blue", "Синий", {"#6aa8e0", "#4f90c8", "#0a1116"},
                      {"#1f5e96", "#174a78", "#f2f7fb"},
                      {"#194d7b", "#143e62", "#f2ece0"}},
    {"green", "Зелёный", {"#79c47a", "#61ad63", "#0b160c"},
                         {"#2f6b33", "#24532a", "#f2f8f1"},
                         {"#255328", "#1e4220", "#f2ece0"}},
    {"red", "Красный", {"#e4675c", "#c9544a", "#1a0b09"},
                       {"#a1362c", "#832a21", "#fbf1ee"},
                       {"#842c24", "#6a231d", "#f2ece0"}},
};

static const int NTHEMES = (int)(sizeof(defs) / sizeof(defs[0]));
static const int NACCENTS = (int)(sizeof(accents) / sizeof(accents[0]));

Theme theme;

bool theme_set(const char *id, const char *accent_id) {
    const ThemeDef *d = &defs[0];
    if (id) {
        for (int i = 0; i < NTHEMES; i++)
            if (!std::strcmp(defs[i].id, id)) { d = &defs[i]; break; }
    }
    theme.id = d->id;
    theme.name = d->name;
    theme.code = d->code;
    theme.family = d->family;
    theme.bg = hex565(d->bg);       theme.bar = hex565(d->bar);
    theme.surf = hex565(d->surf);   theme.surf2 = hex565(d->surf2);
    theme.line = hex565(d->line);   theme.line2 = hex565(d->line2);
    theme.art = hex565(d->art);     theme.fg = hex565(d->fg);
    theme.dim = hex565(d->dim);     theme.accent = hex565(d->accent);
    theme.accent2 = hex565(d->accent2);
    theme.track = hex565(d->track); theme.sel = hex565(d->sel);
    theme.num = hex565(d->num);     theme.note = hex565(d->note);
    theme.onacc = hex565(d->onacc);

    if (accent_id && *accent_id) {
        for (int i = 0; i < NACCENTS; i++) {
            if (std::strcmp(accents[i].id, accent_id)) continue;
            const char *const *tri = accents[i].dark;
            if (theme.family == FAM_LIGHT) tri = accents[i].ink;
            else if (theme.family == FAM_GREY) tri = accents[i].deep;
            theme.accent = hex565(tri[0]);
            theme.accent2 = hex565(tri[1]);
            theme.onacc = hex565(tri[2]);
            break;
        }
    }
    return true;
}

bool theme_init() { return theme_set("graphite", nullptr); }

const char *const *theme_ids(int *n) {
    static const char *ids[16];
    for (int i = 0; i < NTHEMES; i++) ids[i] = defs[i].id;
    if (n) *n = NTHEMES;
    return ids;
}

const char *const *accent_ids(int *n) {
    static const char *ids[16];
    for (int i = 0; i < NACCENTS; i++) ids[i] = accents[i].id;
    if (n) *n = NACCENTS;
    return ids;
}
