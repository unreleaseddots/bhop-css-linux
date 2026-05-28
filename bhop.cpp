#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <sys/uio.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <fstream>
#include <string>

static constexpr uintptr_t OFF_FORCE_JUMP = 0x1017578;
static constexpr uintptr_t OFF_STAMINA    = 0x9c0;
static constexpr uintptr_t OFF_PLAYER     = 0xfa6518;

static std::atomic<bool> g_running  { true  };
static std::atomic<bool> g_bhop_on  { false };
static std::atomic<bool> g_space    { false };
static std::atomic<int>  g_mouse_x  { 0     }; // acumulado de movimento X do mouse

static bool mem_write(pid_t pid, uintptr_t addr, int val) {
    struct iovec l{&val,4}, r{(void*)addr,4};
    return process_vm_writev(pid,&l,1,&r,1,0)==4;
}
static bool mem_write_f(pid_t pid, uintptr_t addr, float val) {
    struct iovec l{&val,4}, r{(void*)addr,4};
    return process_vm_writev(pid,&l,1,&r,1,0)==4;
}
static bool mem_read_ptr(pid_t pid, uintptr_t addr, uintptr_t& val) {
    struct iovec l{&val,8}, r{(void*)addr,8};
    return process_vm_readv(pid,&l,1,&r,1,0)==8;
}

// ── uinput: injeta teclas A/D no jogo ──────────────────────────
static int g_uinput_fd = -1;

static int uinput_init() {
    int fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT,  EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_A);
    ioctl(fd, UI_SET_KEYBIT, KEY_D);

    struct uinput_setup us{};
    us.id.bustype = BUS_USB;
    us.id.vendor  = 0x1234;
    us.id.product = 0x5678;
    strcpy(us.name, "bhop_strafe");

    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    sleep(1); // deixa o kernel registrar
    return fd;
}

static void uinput_key(int fd, int key, int val) {
    struct input_event ev{};
    ev.type  = EV_KEY;
    ev.code  = key;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    write(fd, &ev, sizeof(ev));
}

// ── Seleção de dispositivos ─────────────────────────────────────
static int find_device(bool want_mouse) {
    struct Cand{std::string path,name;int score;};
    std::vector<Cand> cands;
    DIR* d=opendir("/dev/input"); if(!d) return -1;
    struct dirent* e;
    while((e=readdir(d))){
        if(strncmp(e->d_name,"event",5)) continue;
        std::string path=std::string("/dev/input/")+e->d_name;
        int fd=open(path.c_str(),O_RDONLY|O_NONBLOCK); if(fd<0) continue;
        char nm[256]={}; ioctl(fd,EVIOCGNAME(sizeof(nm)),nm);
        std::string sn(nm);
        auto ci=[](const std::string&a,const std::string&b){
            std::string la=a,lb=b;
            for(auto&c:la)c=tolower(c);for(auto&c:lb)c=tolower(c);
            return la.find(lb)!=std::string::npos;};

        if(want_mouse){
            // Quer mouse: deve ter REL_X
            uint8_t bits[REL_MAX/8+1]={};
            if(ioctl(fd,EVIOCGBIT(EV_REL,sizeof(bits)),bits)<0){close(fd);continue;}
            if(!(bits[REL_X/8]&(1<<(REL_X%8)))){close(fd);continue;}
            // Penaliza teclado
            int score=10;
            if(ci(sn,"mouse"))    score+=500;
            if(ci(sn,"keyboard")) score-=1000;
            if(ci(sn,"consumer")) score-=1000;
            if(ci(sn,"system"))   score-=1000;
            cands.push_back({path,sn,score});
        } else {
            // Quer teclado: deve ter KEY_SPACE com ≥50 teclas
            uint8_t bits[KEY_MAX/8+1]={};
            if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)<0){close(fd);continue;}
            if(!(bits[KEY_SPACE/8]&(1<<(KEY_SPACE%8)))){close(fd);continue;}
            int cnt=0;
            for(int i=0;i<KEY_MAX/8+1;++i) for(int b=0;b<8;++b) if(bits[i]&(1<<b)) cnt++;
            if(cnt<50){close(fd);continue;}
            int score=cnt;
            if(ci(sn,"mouse"))    score-=10000;
            if(ci(sn,"consumer")) score-=10000;
            if(ci(sn,"keyboard")) score+=500;
            cands.push_back({path,sn,score});
        }
        close(fd);
    }
    closedir(d);
    if(cands.empty()) return -1;
    std::sort(cands.begin(),cands.end(),[](const Cand&a,const Cand&b){return a.score>b.score;});
    std::cout<<"[+] "<<(want_mouse?"Mouse":"Teclado")<<": "<<cands[0].path<<" \""<<cands[0].name<<"\"\n";
    return open(cands[0].path.c_str(),O_RDONLY|O_NONBLOCK);
}

// ── Threads de input ────────────────────────────────────────────
static void keyboard_thread(int fd){
    struct input_event ev;
    while(g_running){
        ssize_t n=read(fd,&ev,sizeof(ev));
        if(n<(ssize_t)sizeof(ev)){usleep(500);continue;}
        if(ev.type==EV_KEY&&ev.code==KEY_SPACE){
            if(ev.value==1)      g_space.store(true);
            else if(ev.value==0) g_space.store(false);
        }
    }
}

static void mouse_thread(int fd){
    struct input_event ev;
    while(g_running){
        ssize_t n=read(fd,&ev,sizeof(ev));
        if(n<(ssize_t)sizeof(ev)){usleep(200);continue;}
        if(ev.type==EV_REL&&ev.code==REL_X){
            // Acumula movimento X do mouse
            g_mouse_x.fetch_add(ev.value);
        }
    }
}

// ── Terminal ────────────────────────────────────────────────────
static struct termios g_old_term;
static void term_raw_enable(){
    tcgetattr(STDIN_FILENO,&g_old_term);
    struct termios r=g_old_term;
    r.c_lflag&=~(ICANON|ECHO); r.c_cc[VMIN]=1; r.c_cc[VTIME]=0;
    tcsetattr(STDIN_FILENO,TCSANOW,&r);
}
static void term_raw_disable(){tcsetattr(STDIN_FILENO,TCSANOW,&g_old_term);}
static void signal_handler(int){g_running=false;term_raw_disable();}

static void control_thread(){
    while(g_running){
        char c=0;
        if(read(STDIN_FILENO,&c,1)<=0) continue;
        if(c=='9'){std::cout<<"\n[9] Encerrando.\n";g_running=false;break;}
        else if(c=='0'){
            bool cur=g_bhop_on.load();g_bhop_on.store(!cur);
            std::cout<<"\r[0] Bhop+Strafe "<<(!cur?"ATIVADO  ":"DESATIVADO")<<"   "<<std::flush;
        }
    }
}

int main(){
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM,signal_handler);

    std::cout<<"╔══════════════════════════════════════╗\n"
             <<"║  CS:S BunnyHop + Auto-Strafe v21     ║\n"
             <<"║  [0] Toggle   [9] Sair               ║\n"
             <<"╚══════════════════════════════════════╝\n\n";

    pid_t pid=0;
    {FILE* p=popen("pgrep -f cstrike_","r");
     if(p){fscanf(p,"%d",&pid);pclose(p);}}
    if(!pid){std::cerr<<"[ERRO] CS:S não encontrado.\n";return 1;}

    uintptr_t base=0;
    {std::ifstream f("/proc/"+std::to_string(pid)+"/maps");std::string line;
     while(std::getline(f,line))
       if(line.find("client.so")!=std::string::npos)
         {base=std::stoull(line.substr(0,line.find('-')),nullptr,16);break;}}
    if(!base){std::cerr<<"[ERRO] client.so não encontrado.\n";return 1;}

    std::cout<<"[+] PID: "<<pid<<"  base: 0x"<<std::hex<<base<<std::dec<<"\n";

    int kbd_fd = find_device(false);
    if(kbd_fd<0){std::cerr<<"[ERRO] Teclado não encontrado.\n";return 1;}

    int mouse_fd = find_device(true);
    if(mouse_fd<0){std::cerr<<"[AVISO] Mouse não encontrado — auto-strafe desativado.\n";}

    g_uinput_fd = uinput_init();
    if(g_uinput_fd<0)
        std::cerr<<"[AVISO] /dev/uinput não disponível — auto-strafe desativado.\n";
    else
        std::cout<<"[+] uinput OK — auto-strafe ativo\n";

    term_raw_enable();
    std::cout<<"\nPronto! [0] ativa, segure ESPAÇO no CS:S.\n"<<std::flush;

    std::thread t_kbd(keyboard_thread, kbd_fd);
    std::thread t_mouse;
    if(mouse_fd>=0) t_mouse = std::thread(mouse_thread, mouse_fd);
    std::thread t_ctrl(control_thread);

    uintptr_t fj = base + OFF_FORCE_JUMP;
    bool strafe_a=false, strafe_d=false;

    while(g_running){
        if(!g_bhop_on || !g_space){
            mem_write(pid,fj,4);
            // Solta strafes se ativos
            if(strafe_a && g_uinput_fd>=0){ uinput_key(g_uinput_fd,KEY_A,0); strafe_a=false; }
            if(strafe_d && g_uinput_fd>=0){ uinput_key(g_uinput_fd,KEY_D,0); strafe_d=false; }
            usleep(5000);
            continue;
        }

        // ── Zera stamina ─────────────────────────────────────
        uintptr_t player=0;
        mem_read_ptr(pid, base+OFF_PLAYER, player);
        float stamina_before = 0.0f;
        if(player){
            struct iovec l{&stamina_before,4},r{(void*)(player+OFF_STAMINA),4};
            process_vm_readv(pid,&l,1,&r,1,0);
            mem_write_f(pid, player+OFF_STAMINA, 0.0f);
        }

        // ── Bhop ─────────────────────────────────────────────
        // 5 por 28ms, 4 por 2ms — pega quase todo frame de pouso
        mem_write(pid,fj,5);
        usleep(28000);
        mem_write(pid,fj,4);
        usleep(2000);

        // ── Debug ─────────────────────────────────────────────
        {
            static int dbg_count=0;
            int mx_snap = g_mouse_x.load();
            if(dbg_count++%10==0)
                printf("\r  stamina=%.1f  mouse_x=%+d  strafe=%s   ",
                    stamina_before, mx_snap,
                    strafe_a?"A":strafe_d?"D":".");
            fflush(stdout);
        }

        // ── Auto-strafe baseado no mouse ──────────────────────
        if(g_uinput_fd>=0){
            int mx = g_mouse_x.exchange(0); // consome o acumulado

            if(mx > 2){
                // Mouse para direita → strafe D
                if(strafe_a){ uinput_key(g_uinput_fd,KEY_A,0); strafe_a=false; }
                if(!strafe_d){ uinput_key(g_uinput_fd,KEY_D,1); strafe_d=true; }
            } else if(mx < -2){
                // Mouse para esquerda → strafe A
                if(strafe_d){ uinput_key(g_uinput_fd,KEY_D,0); strafe_d=false; }
                if(!strafe_a){ uinput_key(g_uinput_fd,KEY_A,1); strafe_a=true; }
            } else {
                // Mouse parado → solta strafe
                if(strafe_a){ uinput_key(g_uinput_fd,KEY_A,0); strafe_a=false; }
                if(strafe_d){ uinput_key(g_uinput_fd,KEY_D,0); strafe_d=false; }
            }
        }

        usleep(10000);
    }

    // Cleanup
    mem_write(pid,fj,4);
    if(strafe_a && g_uinput_fd>=0) uinput_key(g_uinput_fd,KEY_A,0);
    if(strafe_d && g_uinput_fd>=0) uinput_key(g_uinput_fd,KEY_D,0);
    if(g_uinput_fd>=0){ ioctl(g_uinput_fd,UI_DEV_DESTROY); close(g_uinput_fd); }
    g_running=false;
    close(kbd_fd);
    if(mouse_fd>=0) close(mouse_fd);
    if(t_kbd.joinable())   t_kbd.join();
    if(t_mouse.joinable()) t_mouse.join();
    if(t_ctrl.joinable())  t_ctrl.join();
    term_raw_disable();
    std::cout<<"\n[*] Encerrado.\n";
    return 0;
}
