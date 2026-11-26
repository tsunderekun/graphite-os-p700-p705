#include "graphite.h"
#include "apphost.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

struct Tasks : ScreenView {
    const char *id() const override { return "tasks"; }

    void draw(Gfx &g) override {
        ui_bg(g);
        g.text(24, 60, "ЗАДАЧИ", 24, theme.fg, TF_LIGHT);
        g.hline(24, 88, 432, theme.line);
        const int n = apphost_task_count();
        if (!n) {
            g.text(240, 374, "Нет запущенных приложений", 13, theme.dim, TF_CENTER);
            g.rect(96, 402, 288, 48, theme.surf);
            g.text(240, 420, "Откройте приложение", 13, theme.accent, TF_CENTER);
        } else {
            int row=0;
            for(int i=0;i<2;i++) {
                char title[256] = {}, command[512] = {};
                bool foreground = false, closing = false;
                if (!apphost_task_info(i,title,sizeof title,command,sizeof command,&foreground,&closing)) continue;
                int y=112+row*192; row++;
                g.rect(24,y,432,168,theme.surf); g.frame(24,y,432,168,theme.line);
                g.text(40,y+31,g.fit(title,16,340),16,theme.fg,TF_BOLD);
                std::string exe(command);
                size_t sp = exe.find(' ');
                if (sp != std::string::npos) exe.resize(sp);
                size_t sl = exe.find_last_of('/');
                if (sl != std::string::npos) exe = exe.substr(sl + 1);
                g.text(40,y+60,exe,11,theme.dim);
                g.text(40,y+86,closing?"закрывается":foreground?"активно":"в фоне",12,
                       foreground && !closing ? theme.accent : theme.dim);
                g.rect(24,y+108,280,44,theme.accent); g.text(164,y+123,"ВЕРНУТЬСЯ",12,theme.onacc,TF_CENTER|TF_BOLD);
                g.rect(316,y+108,140,44,theme.surf); g.text(386,y+123,"ЗАКРЫТЬ",12,theme.fg,TF_CENTER|TF_BOLD);
            }
        }
    }

    bool tap(int x, int y) override {
        int row=0;
        for(int i=0;i<2;i++) {
            char title[2], command[2]; bool fg=false, closing=false;
            if(!apphost_task_info(i,title,sizeof title,command,sizeof command,&fg,&closing)) continue;
            int ry=112+row*192; row++;
            if(hit(x,y,24,ry+108,280,44)) { apphost_resume_task(i); app.open("home"); return true; }
            if(hit(x,y,316,ry+108,140,44)) { apphost_close_task(i); app.invalidate(); return true; }
        }
        if (hit(x, y, 96, 402, 288, 48)) { app.open("apps"); return true; }
        return false;
    }
};
}

ScreenView *make_tasks() { return new Tasks(); }
