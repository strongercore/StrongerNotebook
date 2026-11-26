/*
【强者记事本 StrongerNotebook 整体架构文档】
作者：自用私有笔记系统，FLTK GUI + C++17
核心思想：以【原始事件日期】为核心，后续所有复盘、补充、感想全部归集到事件当天文件夹，
不是简单按写入时间流水，方便回溯一件事从头到尾的所有思考记录。

✅【强者核心准则】
已落地保存的历史笔记永久只读、不可篡改；系统只支持追加新记录，禁止回改旧文件。
编辑区 = 当下正在撰写的新想法，不会被历史笔记覆盖；回看仅用于预览查阅。

一、物理目录结构（LOGS文件夹，和exe同级）
LOGS/
├─ index.json                                       # 索引缓存，加速加载；仅为缓存，不是唯一数据源
├─ 年份文件夹（例：2026）
│  ├─ 月份文件夹（例：11）
│  │  └─ YYYYMMDD日期文件夹（例：20261110）
│  │     ├─ 主日志：YYYYMMDD.txt（单日唯一，和文件夹同名）
│  │     ├─ 复盘/回看补充文件（全部放在目标事件的日期文件夹内！不是写笔记当天）
│  │     └─ ……

二、文件命名规则（防覆盖、防重名核心逻辑）
补充文件统称：复盘、回看、追加、感想、分析等
主日志：YYYYMMDD.txt，一个日期文件夹只有一份主日志
补充文件前缀 =【执行补充操作当天的系统日期】YYYYMMDD

分支判定：
 1. 跨天补充（补充日期 ≠ 目标日志日期）
     第1次：YYYYMMDD.txt
     第2次：YYYYMMDD(1).txt
     第3次：YYYYMMDD(2).txt
 2. 当天补充（补充日期 == 目标日志日期，重点防覆盖主日志）
     第1次：YYYYMMDD(1).txt
     第2次：YYYYMMDD(2).txt
     第3次：YYYYMMDD(3).txt

口诀：跨天首次无括号，当天首次直接从(1)开始

三、TXT内嵌元数据（双保险，防止index.json损坏丢失信息）
所有txt文件头部固定两行，记事本打开可见，程序预览时自动跳过这两行：
#tags:标签1,标签2
#title:自定义笔记标题
优势：手动修改文件、索引炸掉的时候，标签和标题不会丢；重建索引优先读取文件头部

四、index.json 索引结构（双表双向索引）
顶层固定两个key：date_map、tag_map
1. date_map：文件 → 标签映射
key规则：目标日期文件夹名 + 文件名，全局唯一，不会重复
value对象：{ "title": "自定义标题", "tags": ["标签1","标签2"] }
示例key："2026111020261110.txt"

2. tag_map：标签 → 文件映射
key规则：标签文本
value：数组，存放绑定该标签的全部文件唯一key（date_map的key）
作用：双向查找，支持从标签反向检索对应的日志文件

五、按钮行为定义
【新建今日日志】：不依赖选中项
判定规则：
   ✅ 允许新建：今日文件夹不存在  OR 文件夹存在但里面没有 yyyyMMdd.txt主日志
   ❌ 禁止新建：今日文件夹存在 并且 文件夹内存在 yyyyMMdd.txt主日志
   新建成功后自动生成主日志，刷新目录树

【追加复盘】：必须选中左侧笔记/日期文件夹
读取编辑区内容，放到选中项所属日期文件夹，文件名是今天日期，自动加(1)(2)去重
检验&重建索引：全盘递归扫描LOGS下所有txt，读取头部#title/#tags，直接覆盖重建index.json，兜底修复索引错乱
恢复目录树：单纯重新渲染一遍树，不扫描磁盘

【增修标签】：输入框有文字，无选中标签=新增；选中标签=修改当前标签
【删除标签】：选中标签后，删除该标签
【已移除：清空草稿按钮】

六、左侧Fl_Tree展示规则
优先展示自定义title；title为空，则展示文件名；年、月节点默认折叠，避免条目过多过长

七、容错设计
元数据内嵌在txt：索引文件损坏、误删、程序闪退，笔记本体自带标题标签
检验&重建索引按钮：手动增删改txt文件后，可以一键同步索引

八、权限规则
所有历史文件只预览，本程序只修改元数据（标签、标题），正文永远不可修改，恪守“只追加、不改过去正文”
*/

// ===================== 头文件引入区  =====================
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>
#include <windows.h>

#include <string>     // std::string 字符串
#include <chrono>     // 时间库，获取系统当前时间
#include <iomanip>    // 格式化输出（日期补0，例如09而不是9）
#include <sstream>    // 字符串流，拼接字符串
#include <filesystem> // C++17 文件系统：创建文件夹、遍历文件、判断文件是否存在
#include <fstream>    // 文件读写 ifstream读、ofstream写
#include <vector>     // 动态数组，存标签列表
#include <regex>      // 正则表达式，解析txt头部#title #tags元数据
#include <algorithm>  // std::find 用来标签去重
#define JSON_USE_IMPLICIT_CONVERSIONS 0
#define JSON_DISABLE_ENUM_SERIALIZATION 1
#include "nlohmann/json.hpp"
namespace fs = std::filesystem;

using json = nlohmann::json;

json g_index;
/// g_logs_path：LOGS文件夹完整路径字符串，例如 "D:\xxx\StrongerNotebook\LOGS"
std::string g_logs_path;
/// g_index_path：index.json完整路径字符串
std::string g_index_path;
/// g_sel_folder：当前选中笔记所属的【目标日期文件夹】（YYYYMMDD格式字符串）
/// 追加复盘的时候，代码要靠这个变量知道：复盘文件放到哪一天的文件夹里
std::string g_sel_folder;
/// g_sel_filename：当前选中的txt文件名，辅助使用
std::string g_sel_filename;
/// g_sel_key：当前选中笔记的索引key，标签编辑用
std::string g_sel_key;
/// g_current_tags：当前选中笔记的标签列表，在内存里操作标签
std::vector<std::string> g_current_tags;

Fl_Hold_Browser *browser_tag = nullptr;

std::string g_selected_tag_str;
// UI控件指针：全部是指针，存堆内存地址
// 👉 所有new出来的FLTK控件，交给FLTK窗口管理，窗口销毁时FLTK自动释放内存，不用手动delete
Fl_Box *title_tree = nullptr;
Fl_Tree *tree_nav = nullptr;
Fl_Box *title_preview = nullptr;
Fl_Text_Display *disp_view = nullptr;
Fl_Text_Buffer *buf_preview = nullptr; // 预览区文本缓冲区，独立内存
Fl_Box *title_draft = nullptr;
Fl_Text_Editor *edit_draft = nullptr;
Fl_Text_Buffer *buf_draft = nullptr; // 草稿编辑区缓冲区，独立内存，和预览完全隔离！【强者规则核心】
Fl_Box *title_tag = nullptr;
Fl_Input *input_tag = nullptr;
Fl_Button *btn_tag_upsert = nullptr; // 合并后的增修标签按钮
Fl_Button *btn_del_tag = nullptr;
Fl_Button *btn_new_today = nullptr;     // 【新建今日日志】按钮
Fl_Button *btn_append_review = nullptr; // 【追加复盘】按钮
Fl_Button *btn_refresh = nullptr;
Fl_Button *btn_restore = nullptr;
Fl_Button *btn_tag_search = nullptr; // 【新增】搜索选中标签按钮，用来触发标签检索，标签按钮本身只做选中标记，不自动搜索
Fl_Button *btn_tag_show_all = nullptr;
Fl_Button *preview_title_btn = nullptr; // 预览区顶部，只读标题展示按钮（无回调）
Fl_Input *title_input = nullptr;        // 草稿上方标题输入框
Fl_Box *slogan_preview;
Fl_Box *slogan_draft;

// ===================== 函数声明区 =====================
// 标签筛选开关、当前选中标签
bool g_tag_filter = false;
std::string g_filter_tag;
bool g_tag_panel_show_all = false; // 标记：true=显示全部标签，false=当前笔记标签

void show_center_msg(const char *msg);
bool save_index();
bool load_index();
bool init_dir();
std::string get_today_ymd();
std::pair<std::string, std::vector<std::string>> read_meta(const fs::path &fp);
bool write_meta_to_file(const fs::path &fp, const std::string &new_title, const std::vector<std::string> &new_tags);
std::string gen_supp_filename(const std::string &target_ymd, const std::string &now_ymd, const fs::path &folder);
void render_tree();
void refresh_tag_panel();

// ---------------- 回调函数----------------
void cb_msg_close(Fl_Widget *, void *win_ptr);
void cb_refresh_index(Fl_Widget *, void *);
void cb_restore_tree(Fl_Widget *, void *);
void cb_new_today_log(Fl_Widget *, void *); // 【今日日志回调】
void cb_append_review(Fl_Widget *, void *); // 【追加复盘回调】
void cb_tree_click(Fl_Widget *, void *);
void cb_tag_upsert(Fl_Widget *, void *);
void cb_tag_delete(Fl_Widget *, void *);
void cb_win_close(Fl_Widget *, void *);
void cb_tag_search(Fl_Widget *w, void *data); // 标签搜索回调：根据g_selected_tag_idx选中的标签，筛选文件，刷新左侧树
void cb_browser_tag(Fl_Widget *, void *);
void update_index_tags(const std::string &key, const std::vector<std::string> &new_tags);
// ===================== 函数实现区  =====================
// ===================== 自制居中弹窗实现 =====================
void show_center_msg(const char *msg)
{
    const int MSG_W = 420;
    const int MSG_H = 140;
    int scrW = Fl::w();
    int scrH = Fl::h();
    int x = (scrW - MSG_W) / 2;
    int y = (scrH - MSG_H) / 2;

    Fl_Window *msg_win = new Fl_Window(x, y, MSG_W, MSG_H, "提示");
    msg_win->begin();
    Fl_Box *box_txt = new Fl_Box(10, 10, MSG_W - 20, MSG_H - 50, msg);
    box_txt->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    box_txt->labelsize(14);
    Fl_Button *btn_ok = new Fl_Button(MSG_W / 2 - 60, MSG_H - 40, 120, 28, "确定");
    btn_ok->callback(cb_msg_close, msg_win);
    msg_win->end();
    msg_win->set_modal();
    msg_win->show();

    // FLTK1.3 标准阻塞循环，替代错误的Fl::modal() / 嵌套Fl::run()
    while (msg_win->shown())
    {
        Fl::wait();
    }
    delete msg_win;
}

// 关闭回调，删掉release_modal，只隐藏窗口
void cb_msg_close(Fl_Widget *, void *win_ptr)
{
    Fl_Window *w = (Fl_Window *)win_ptr;
    w->hide(); // 只hide，上面while循环检测shown()就会退出
}


bool save_index()
{
    std::ofstream f(g_index_path);
    if (!f.is_open())
        return false;
    f << g_index.dump(4);
    return true;
}

bool load_index()
{
    if (!fs::exists(g_index_path))
    {
        g_index["date_map"] = json::object();
        g_index["tag_map"] = json::object();
        return save_index();
    }
    std::ifstream f(g_index_path);
    if (!f.is_open())
        return false;
    try
    {
        f >> g_index;
        if (!g_index.contains("date_map"))
            g_index["date_map"] = json::object();
        if (!g_index.contains("tag_map"))
            g_index["tag_map"] = json::object();
        if (!g_index.is_object())
        {
            g_index = json::object();
            save_index();
        }
        return true;
    }
    catch (...)
    {
        g_index["date_map"] = json::object();
        g_index["tag_map"] = json::object();
        return false;
    }
}

bool init_dir()
{
    fs::path exe_cwd = fs::current_path();
    fs::path logs_path_obj = exe_cwd / "LOGS";
    fs::path index_path_obj = logs_path_obj / "index.json";
    g_logs_path = logs_path_obj.string();
    g_index_path = index_path_obj.string();

    if (!fs::exists(logs_path_obj))
    {
        std::error_code ec;
        fs::create_directory(logs_path_obj, ec);
        if (ec)
            return false;
    }
    return load_index();
}

std::string get_today_ymd()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm *tm = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y%m%d");
    return oss.str();
}

std::pair<std::string, std::vector<std::string>> read_meta(const fs::path &fp)
{
    std::string title = "";
    std::vector<std::string> tags;
    std::ifstream f(fp);
    if (!f.is_open())
        return {title, tags};

    std::string line;
    std::regex re_title(R"(#title:\s*(.*))");
    std::regex re_tags(R"(#tags:\s*(.*))");
    std::smatch m;

    while (std::getline(f, line))
    {
        if (std::regex_match(line, m, re_title))
        {
            title = m[1].str();
        }
        else if (std::regex_match(line, m, re_tags))
        {
            std::string tagstr = m[1].str();
            std::istringstream iss(tagstr);
            std::string t;
            while (std::getline(iss, t, ','))
            {
                if (!t.empty())
                    tags.push_back(t);
            }
        }
        else
            break;
    }
    return {title, tags};
}

// 【核心】重写文件头部元数据，正文原样读取、原样写回！不修改笔记正文
bool write_meta_to_file(const fs::path &fp, const std::string &new_title, const std::vector<std::string> &new_tags)
{
    std::ifstream fin(fp);
    if (!fin.is_open())
        return false;
    std::stringstream body_ss;
    std::string line;

    // 跳过原有两行元数据，读取正文
    int meta_line_cnt = 0;
    while (std::getline(fin, line))
    {
        if (meta_line_cnt < 2)
        {
            if (line.substr(0, 6) == "#tags:" || line.substr(0, 7) == "#title:")
            {
                meta_line_cnt++;
                continue;
            }
        }
        body_ss << line << "\n";
    }
    fin.close();

    // 拼接新的#tags行
    std::string tag_line = "#tags:";
    for (size_t i = 0; i < new_tags.size(); i++)
    {
        if (i > 0)
            tag_line += ",";
        tag_line += new_tags[i];
    }
    std::string title_line = "#title:" + new_title;

    // 写入新头部 + 原正文
    std::ofstream fout(fp);
    if (!fout.is_open())
        return false;
    fout << tag_line << "\n";
    fout << title_line << "\n";
    fout << body_ss.str();
    fout.close();
    return true;
}

// 生成复盘补充文件名，严格匹配文档命名规则
std::string gen_supp_filename(const std::string &target_ymd, const std::string &now_ymd, const fs::path &folder)
{
    std::string base = now_ymd;
    bool same_day = (now_ymd == target_ymd);
    std::vector<std::string> exist_names;
    std::error_code ec;

    // 遍历目标日期文件夹，收集所有同前缀txt文件
    for (auto &e : fs::directory_iterator(folder, ec))
    {
        if (!e.is_regular_file())
            continue;
        auto p = e.path();
        if (p.extension() != ".txt")
            continue;
        exist_names.push_back(p.stem().string());
    }

    int cnt = 0;
    // 只统计【补充文件】：base(数字)，主文件 base 本身不算补充文件
    for (auto &s : exist_names)
    {
        if (s.find(base) == 0)
        {
            if (s != base) // 关键修复：过滤掉纯主日志，不计数
            {
                cnt++;
            }
        }
    }

    if (!same_day)
    {
        // 跨天复盘：第一次直接 base.txt，后续从(1)开始递增
        if (cnt == 0)
        {
            return base + ".txt";
        }
        else
        {
            return base + "(" + std::to_string(cnt) + ").txt";
        }
    }
    else
    {
        // 当天复盘：主日志已经存在，补充文件从 (1) 开始
        return base + "(" + std::to_string(cnt + 1) + ").txt";
    }
}

// 根据key和新标签列表，更新date_map和tag_map的关联
void update_index_tags(const std::string &key, const std::vector<std::string> &new_tags)
{
    g_index["date_map"][key]["tags"] = new_tags;
    auto &tag_map = g_index["tag_map"];

    // 先清除这条笔记在所有旧标签里的关联
    for (auto &[t, arr] : tag_map.items())
    {
        auto vec = arr.get<std::vector<std::string>>();
        auto it = std::find(vec.begin(), vec.end(), key);
        if (it != vec.end())
        {
            vec.erase(it);
        }
        if (vec.empty())
            tag_map.erase(t);
        else
            arr = vec;
    }
    // 绑定新标签
    for (auto &t : new_tags)
    {
        if (!tag_map.contains(t))
            tag_map[t] = json::array();
        auto &arr = tag_map[t];
        bool exist = false;
        for (auto &item : arr)
        {
            if (item == key)
            {
                exist = true;
                break;
            }
        }
        if (!exist)
            arr.push_back(key);
    }
}

void render_tree()
{
    Fl_Tree_Item *p = tree_nav->first();
    while (p != nullptr)
    {
        void *ud = p->user_data();
        if (ud != nullptr)
        {
            std::string *s_ptr = static_cast<std::string *>(ud);
            delete s_ptr;
            p->user_data(nullptr);
        }
        p = tree_nav->next(p);
    }
    tree_nav->clear();

    // 判断：是否开启标签筛选
    if (g_tag_filter)
    {
        // 筛选模式：拿tag_map里这个标签对应的所有key
        if (g_index["tag_map"].contains(g_filter_tag) && g_index["tag_map"][g_filter_tag].is_array())
        {
            for (auto &key_json : g_index["tag_map"][g_filter_tag])
            {
                if (!key_json.is_string())
                    continue;
                std::string key = key_json.get<std::string>();
                if (!g_index["date_map"].contains(key))
                    continue;
                auto &val = g_index["date_map"][key];

                std::string folder_name = key.substr(0, 8);
                std::string fname = key.substr(8);
                std::string y = folder_name.substr(0, 4);
                std::string m = folder_name.substr(4, 2);

                std::string title = "";
                if (val.contains("title") && val["title"].is_string())
                {
                    title = val["title"].get<std::string>();
                }
                std::string show_name = title.empty() ? fname : title;
                // 重点：path末尾用fname保证唯一，避免同名title合并
                std::string path = y + "/" + m + "/" + folder_name + "/" + fname;

                Fl_Tree_Item *item = tree_nav->add(path.c_str());
                if (item)
                {
                    // 修改显示文字为title，不改动path
                    item->label(show_name.c_str());
                    std::string *p_data = new std::string(key);
                    item->user_data(p_data);
                }
            }
        }
    }
    else
    {
        // 默认：渲染全部date_map
        for (auto &[key, val] : g_index["date_map"].items())
        {
            std::string folder_name = key.substr(0, 8);
            std::string fname = key.substr(8);
            std::string y = folder_name.substr(0, 4);
            std::string m = folder_name.substr(4, 2);

            std::string title = "";
            if (val.contains("title") && val["title"].is_string())
            {
                title = val["title"].get<std::string>();
            }
            std::string show_name = title.empty() ? fname : title;
            // path用fname（原始文件名）保证唯一性
            std::string path = y + "/" + m + "/" + folder_name + "/" + fname;

            Fl_Tree_Item *item = tree_nav->add(path.c_str());
            if (item)
            {
                // 覆盖label，展示title
                item->label(show_name.c_str());
                std::string *p_data = new std::string(key);
                item->user_data(p_data);
            }
        }
    }
    tree_nav->redraw();
}

void refresh_tag_panel()
{
    // 清空browser所有行，原生接口，稳定无bug
    if (!browser_tag || !input_tag)
    {
        printf("[ERR] refresh_tag_panel: browser_tag 或者 input_tag 是空\n");
        return;
    }
    browser_tag->clear();
    g_selected_tag_str = "";
    input_tag->value("");

    if (g_tag_panel_show_all)
    {
        // ========== 模式A：展示全部全局标签（从tag_map读取） ==========
        if (g_index.contains("tag_map") && g_index["tag_map"].is_object())
        {
            for (auto &[tag_name, arr] : g_index["tag_map"].items())
            {
                browser_tag->add(tag_name.c_str());
            }
        }
    }
    else
    {
        // ========== 模式B：只展示当前选中笔记的标签 g_current_tags ==========
        for (auto &tag : g_current_tags)
        {
            browser_tag->add(tag.c_str());
        }
    }
    browser_tag->redraw();
}

void cb_win_close(Fl_Widget *w, void *)
{

    Fl_Tree_Item *p = tree_nav->first();
    while (p != nullptr)
    {
        void *ud = p->user_data();
        if (ud != nullptr)
        {
            std::string *s_ptr = static_cast<std::string *>(ud);
            delete s_ptr;
            p->user_data(nullptr);
        }
        p = tree_nav->next(p);
    }
    tree_nav->clear();
    w->hide();
    delete buf_preview;
    delete buf_draft;
}

void cb_refresh_index(Fl_Widget *, void *)
{
    // 初始化双表结构
    g_index = json::object();
    g_index["date_map"] = json::object();
    g_index["tag_map"] = json::object();
    fs::path logs(g_logs_path);
    std::error_code ec;

    for (auto &ye : fs::directory_iterator(logs, ec))
    {
        if (ec)
        {
            printf("[WARN] 遍历年份目录出错: %s\n", ec.message().c_str());
            ec.clear();
            continue;
        }
        if (!ye.is_directory())
            continue;
        std::string yname = ye.path().filename().string();
        for (auto &me : fs::directory_iterator(ye.path(), ec))
        {
            if (ec)
            {
                printf("[WARN] 遍历月份目录出错: %s\n", ec.message().c_str());
                ec.clear();
                continue;
            }
            if (!me.is_directory())
                continue;
            std::string mname = me.path().filename().string();
            for (auto &de : fs::directory_iterator(me.path(), ec))
            {
                if (ec)
                {
                    printf("[WARN] 遍历日期目录出错: %s\n", ec.message().c_str());
                    ec.clear();
                    continue;
                }
                if (!de.is_directory())
                    continue;
                std::string dname = de.path().filename().string();
                if (dname.size() != 8)
                    continue;
                for (auto &fe : fs::directory_iterator(de.path(), ec))
                {
                    if (ec)
                    {
                        printf("[WARN] 遍历文件出错: %s\n", ec.message().c_str());
                        ec.clear();
                        continue;
                    }
                    if (!fe.is_regular_file())
                        continue;

                    // 后缀大小写兼容
                    std::string ext = fe.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c){ return std::tolower(c); });
                    if (ext != ".txt")
                        continue;

                    std::string title;
                    std::vector<std::string> tags;
                    try
                    {
                        std::tie(title, tags) = read_meta(fe.path());
                    }
                    catch (...)
                    {
                        printf("[ERR] read_meta读取失败: %s\n", fe.path().string().c_str());
                        continue;
                    }

                    std::string fname = fe.path().filename().string();
                    std::string key = dname + fname;

                    // ✅ 日志写入 date_map，逻辑不变
                    g_index["date_map"][key] = json::object();
                    g_index["date_map"][key]["title"] = title;
                    g_index["date_map"][key]["tags"] = tags;

                    // ✅ 同步构建 tag_map：同标签内key去重，跨标签允许重复
                    for (auto &t : tags)
                    {
                        if (!g_index["tag_map"].contains(t))
                        {
                            g_index["tag_map"][t] = json::array();
                        }
                        auto &arr = g_index["tag_map"][t];
                        bool has_key = false;
                        for (auto &item : arr)
                        {
                            if (item.get<std::string>() == key)
                            {
                                has_key = true;
                                break;
                            }
                        }
                        if (!has_key)
                        {
                            arr.push_back(key);
                        }
                    }
                }
            }
        }
    }
    save_index();
    render_tree();
    refresh_tag_panel();
}

void cb_restore_tree(Fl_Widget *, void *)
{
    // 关闭标签过滤，回到完整时间目录
    g_tag_filter = false;
    g_filter_tag = "";
    render_tree();
}

// 删除标签回调
void cb_tag_delete(Fl_Widget *, void *)
{
    // 新增拦截：全局标签模式禁止删除标签
    if (g_tag_panel_show_all)
    {
        show_center_msg("当前是全局标签浏览模式，仅支持检索，不能删除标签！");
        return;
    }

    if (g_sel_key.empty())
    {
        show_center_msg("请先在左侧选中一条笔记！");
        return;
    }
    if (g_selected_tag_str.empty())
    {
        show_center_msg("请先点击选中一个标签！");
        return;
    }

    std::string folder = g_sel_key.substr(0, 8);
    std::string fname = g_sel_key.substr(8);
    fs::path fp = fs::path(g_logs_path) / folder.substr(0, 4) / folder.substr(4, 2) / folder / fname;
    auto [title, old_tags] = read_meta(fp);
    auto it_tag = std::find(old_tags.begin(), old_tags.end(), g_selected_tag_str);
    if (it_tag != old_tags.end())
    {
        old_tags.erase(it_tag);
    }

    if (!write_meta_to_file(fp, title, old_tags))
    {
        show_center_msg("删除标签失败！");
        return;
    }

    std::string key = g_sel_key;
    update_index_tags(key, old_tags);

    save_index();
    g_current_tags = old_tags;
    refresh_tag_panel();
    show_center_msg("✅ 标签已删除！");
}

// 增修标签回调：新增 / 修改标签
void cb_tag_upsert(Fl_Widget *, void *)
{
    // 新增拦截：全局标签模式禁止增改标签
    if (g_tag_panel_show_all)
    {
        show_center_msg("当前是全局标签浏览模式，仅支持检索，不能增改标签！");
        return;
    }

    if (g_sel_key.empty())
    {
        show_center_msg("请先在左侧选中一条笔记！");
        return;
    }
    std::string new_tag = input_tag->value();
    if (new_tag.empty())
    {
        show_center_msg("标签内容不能为空！");
        return;
    }

    // 拿到当前笔记的文件路径
    std::string folder = g_sel_key.substr(0, 8);
    std::string fname = g_sel_key.substr(8);
    fs::path fp = fs::path(g_logs_path) / folder.substr(0, 4) / folder.substr(4, 2) / folder / fname;
    auto [title, old_tags] = read_meta(fp);

    if (g_selected_tag_str.empty())
    {
        // 新增标签，去重
        auto it = std::find(old_tags.begin(), old_tags.end(), new_tag);
        if (it != old_tags.end())
        {
            show_center_msg("该标签已经存在！");
            return;
        }
        old_tags.push_back(new_tag);
    }
    else
    {
        // 修改标签
        auto it = std::find(old_tags.begin(), old_tags.end(), g_selected_tag_str);
        if (it != old_tags.end())
        {
            *it = new_tag;
        }
    }

    // 写回文件元数据
    if (!write_meta_to_file(fp, title, old_tags))
    {
        show_center_msg("标签写入文件失败！");
        return;
    }

    std::string key = g_sel_key;
    update_index_tags(key, old_tags);

    save_index();
    g_current_tags = old_tags;
    refresh_tag_panel();
    show_center_msg("✅ 标签保存成功！");
}

// 【新建今日日志回调函数】
void cb_new_today_log(Fl_Widget *, void *)
{
    std::string new_title = title_input->value();
    if (new_title.empty())
    {
        show_center_msg("提示：请输入笔记标题！");
        return;
    }

    std::string raw_content = buf_draft->text();
    // 去除首尾空白字符，判断是否完全空内容（空格、换行、tab都不算有效内容）
    size_t start = raw_content.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
    {
        show_center_msg("提示：编辑区内容为空，无法新建日志！");
        return;
    }

    std::string d = get_today_ymd();
    std::string y = d.substr(0, 4);
    std::string m = d.substr(4, 2);
    fs::path date_folder = fs::path(g_logs_path) / y / m / d;
    fs::path main_file = date_folder / (d + ".txt");
    std::error_code ec;

    // 核心判定：文件夹存在 并且 主日志文件存在 → 禁止新建
    if (fs::exists(date_folder) && fs::exists(main_file))
    {
        show_center_msg("提示：今日主日志已经存在！\n请使用【追加复盘】来新增记录。");
        return;
    }

    // 文件夹不存在，逐级创建
    if (!fs::exists(date_folder))
    {
        fs::create_directories(date_folder, ec);
        if (ec)
        {
            show_center_msg("错误：无法创建日期文件夹！");
            return;
        }
    }

    std::string content = raw_content;
    std::ofstream f(main_file);
    if (!f.is_open())
    {
        show_center_msg("错误：无法写入主日志文件！");
        return;
    }
    f << "#tags:\n#title:" << new_title << "\n";
    f << content;
    f.close();

    // 更新索引
    auto [title, tags] = read_meta(main_file);
    std::string key = d + main_file.filename().string();
    update_index_tags(key, tags);

    if (save_index())
    {
        render_tree();
        buf_draft->text(""); // 写入成功清空草稿
        title_input->value("");
        show_center_msg("✅ 今日主日志创建成功！");
    }
    else
    {
        show_center_msg("警告：文件写入成功\n但是索引保存失败！建议点【刷新索引】");
    }
}

// 【追加复盘回调函数】
void cb_append_review(Fl_Widget *, void *)
{
    // 前置判断：必须选中文件夹/文件
    if (g_sel_folder.empty())
    {
        show_center_msg("提示：请先在左侧目录选中一条日志\n或日期文件夹，再做复盘追加！");
        return;
    }

    std::string new_title = title_input->value();
    if (new_title.empty())
    {
        show_center_msg("提示：请输入复盘标题！");
        return;
    }

    std::string raw_content = buf_draft->text();
    // 同样拦截空白草稿，防止生成空复盘文件
    size_t start = raw_content.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
    {
        show_center_msg("提示：编辑区内容为空，无法追加复盘！");
        return;
    }

    std::string now_ymd = get_today_ymd();
    fs::path target_folder = fs::path(g_logs_path) / g_sel_folder.substr(0, 4) / g_sel_folder.substr(4, 2) / g_sel_folder;
    std::string new_fname = gen_supp_filename(g_sel_folder, now_ymd, target_folder);
    fs::path fp = target_folder / new_fname;

    std::string content = raw_content;
    std::ofstream f(fp);
    if (!f.is_open())
    {
        show_center_msg("错误：复盘文件写入失败！");
        return;
    }
    f << "#tags:\n#title:" << new_title << "\n";
    f << content;
    f.close();

    auto [title, tags] = read_meta(fp);
    std::string key = g_sel_folder + fp.filename().string();
    update_index_tags(key, tags);

    if (save_index())
    {
        render_tree();
        buf_draft->text("");
        title_input->value("");
        show_center_msg("✅ 复盘追加成功！");
    }
    else
    {
        show_center_msg("警告：复盘文件写入成功\n索引保存失败，建议刷新索引！");
    }
}

void cb_tree_click(Fl_Widget *, void *)
{
    Fl_Tree_Item *item = tree_nav->last_selected_item();
    if (!item)
    {
        // 没选中，清空标签面板
        g_sel_key = "";
        g_sel_folder = "";
        g_current_tags.clear();
        g_tag_panel_show_all = false; // 切回单笔记标签模式
        refresh_tag_panel();
        buf_preview->text("");
        // ✅ 修改：copy_label
        preview_title_btn->copy_label("");
        preview_title_btn->redraw();
        return;
    }

    void *ud = item->user_data();
    if (!ud)
        return;
    std::string *pkey = (std::string *)ud;
    std::string key = *pkey;
    g_sel_key = key;
    std::string folder = key.substr(0, 8);
    std::string fname = key.substr(8);
    g_sel_folder = folder;
    g_sel_filename = fname;

    fs::path fp = fs::path(g_logs_path) / folder.substr(0, 4) / folder.substr(4, 2) / folder / fname;
    auto [title, tags] = read_meta(fp);
    std::string show_title = title.empty() ? "无标题" : title;
    // ✅ 修改：这里换成 copy_label，show_title 是局部string也安全
    preview_title_btn->copy_label(show_title.c_str());
    preview_title_btn->redraw();
    g_current_tags = tags;
    g_tag_panel_show_all = false; // 点笔记，自动关闭全局标签模式
    refresh_tag_panel();

    std::ifstream f(fp);
    if (!f.is_open())
    {
        // ✅ 修改：copy_label
        preview_title_btn->copy_label("");
        preview_title_btn->redraw();
        buf_preview->text("");
        return;
    }
    std::stringstream ss;
    std::string line;
    bool skip_meta = true;
    while (std::getline(f, line))
    {
        if (skip_meta)
        {
            if (line.substr(0, 6) == "#tags:" || line.substr(0, 7) == "#title:")
                continue;
            else
                skip_meta = false;
        }
        ss << line << "\n";
    }
    buf_preview->text(ss.str().c_str());
}

// 标签Browser点击回调：选中一行，拿到标签文本，填入输入框
void cb_browser_tag(Fl_Widget *, void *)
{
    // 拿到选中行号
    int sel_line = browser_tag->value();
    if (sel_line < 1)
    {
        // 取消选中
        g_selected_tag_str = "";
        input_tag->value("");
        return;
    }
    // 取出该行标签文字
    const char *tag_txt = browser_tag->text(sel_line);
    g_selected_tag_str = tag_txt;
    input_tag->value(tag_txt);
}


void cb_tag_search(Fl_Widget *w, void *data)
{
    if (g_selected_tag_str.empty())
    {
        show_center_msg("请先点击选中一个标签！");
        return;
    }
    g_filter_tag = g_selected_tag_str;
    g_tag_filter = true;
    render_tree();
}

void cb_tag_show_all(Fl_Widget *, void *)
{
    g_tag_panel_show_all = true;
    g_selected_tag_str = "";
    input_tag->value("");
    refresh_tag_panel();
}

// ===================== UI布局常量 =====================
const int WINDOW_W = 1400;
const int WINDOW_H = 720;
const int MIN_W = 800;
const int MIN_H = 500;
const int TREE_W = 230;
const int TAG_W = 230;
const int BTN_H = 28;
const int GAP = 3;
const int TITLE_H = 18;
const int MID_GAP = 2;

class MyWindow : public Fl_Window
{
public:
    MyWindow(int w, int h, const char *t) : Fl_Window(w, h, t) {}
    void resize(int X, int Y, int W, int H) override
    {
        Fl_Window::resize(X, Y, W, H);
        int winW = this->w();
        int winH = this->h();
        const int INNER_PAD = 2;

        const int LEFT_BTN_AREA = BTN_H * 4 + GAP * 4;
        const int RIGHT_BTN_AREA = BTN_H * 5 + GAP * 4;

        int leftBlockX = GAP;
        int leftBlockW = TREE_W;

        int tagBlockX = winW - TAG_W - GAP;
        int tagBlockW = TAG_W;

        int midBlockX = leftBlockX + leftBlockW + GAP;
        int midBlockW = tagBlockX - midBlockX - GAP;
        if (midBlockW < 0)
            midBlockW = 0;

        // ========== 左侧区块（不变） ==========
        title_tree->resize(leftBlockX + INNER_PAD, GAP, leftBlockW - INNER_PAD * 2, BTN_H);
        title_tree->labelfont(FL_HELVETICA_BOLD);
        title_tree->labelsize(18);
        title_tree->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        int treeY = GAP + TITLE_H + GAP;
        int treeH = winH - treeY - LEFT_BTN_AREA - GAP;
        if (treeH < 40)
            treeH = 40;
        tree_nav->resize(leftBlockX + INNER_PAD, treeY, leftBlockW - INNER_PAD * 2, treeH);
        int leftScrollBottomY = tree_nav->y() + tree_nav->h();

        int btnY = leftScrollBottomY + GAP;
        btn_new_today->resize(leftBlockX + INNER_PAD, btnY, leftBlockW - INNER_PAD * 2, BTN_H);
        btnY += BTN_H + GAP;
        btn_append_review->resize(leftBlockX + INNER_PAD, btnY, leftBlockW - INNER_PAD * 2, BTN_H);
        btnY += BTN_H + GAP;
        btn_restore->resize(leftBlockX + INNER_PAD, btnY, leftBlockW - INNER_PAD * 2, BTN_H);
        btnY += BTN_H + GAP;
        btn_refresh->resize(leftBlockX + INNER_PAD, btnY, leftBlockW - INNER_PAD * 2, BTN_H);

        // ========== 右侧标签区块（不变） ==========
        title_tag->resize(tagBlockX + INNER_PAD, GAP, tagBlockW - INNER_PAD * 2, BTN_H);
        title_tag->labelfont(FL_HELVETICA_BOLD);
        title_tag->labelsize(18);
        title_tag->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        int tagScrollY = GAP + TITLE_H + GAP;
        int tagBottomTotal = RIGHT_BTN_AREA + GAP;
        int tagScrollH = winH - tagScrollY - tagBottomTotal - GAP;
        if (tagScrollH < 20)
            tagScrollH = 20;
        browser_tag->resize(tagBlockX + INNER_PAD, tagScrollY, tagBlockW - INNER_PAD * 2, tagScrollH);
        int rightScrollBottomY = browser_tag->y() + browser_tag->h();

        int tagBtnY = rightScrollBottomY + GAP;
        input_tag->resize(tagBlockX + INNER_PAD, tagBtnY, tagBlockW - INNER_PAD * 2, BTN_H);
        tagBtnY += BTN_H + GAP;
        btn_tag_upsert->resize(tagBlockX + INNER_PAD, tagBtnY, tagBlockW - INNER_PAD * 2, BTN_H);
        tagBtnY += BTN_H + GAP;
        btn_del_tag->resize(tagBlockX + INNER_PAD, tagBtnY, tagBlockW - INNER_PAD * 2, BTN_H);
        tagBtnY += BTN_H + GAP;
        btn_tag_search->resize(tagBlockX + INNER_PAD, tagBtnY, tagBlockW - INNER_PAD * 2, BTN_H);
        tagBtnY += BTN_H + GAP;
        btn_tag_show_all->resize(tagBlockX + INNER_PAD, tagBtnY, tagBlockW - INNER_PAD * 2, BTN_H);

        // ========== 中间区块【修改完成，严格对齐基准】 ==========

        int previewTitleY = GAP;
        const int labelFixedW = 60;
        const int OFFSET = 32;
        const int SLOGAN_W = 300; 

        int textLeft = midBlockX + INNER_PAD;
        int textRight = midBlockX + midBlockW - INNER_PAD;
        int ctrlW = 300;

        // ✅ 总空间 = 中间文本区域总宽，减去标语宽度，标题只在【左边这块区域】居中
        int totalMidW = textRight - textLeft;
        int titleAreaW = totalMidW - SLOGAN_W; // 标题专属区域，标语占掉右边一块
        int midAvailW = titleAreaW - labelFixedW;

        int midX = textLeft + labelFixedW + (midAvailW - ctrlW) / 2 + OFFSET;

        title_preview->resize(textLeft, previewTitleY, labelFixedW, BTN_H);
        title_preview->labelfont(FL_HELVETICA_BOLD);
        title_preview->labelsize(18);
        title_preview->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CENTER);

        preview_title_btn->resize(midX, previewTitleY, ctrlW, BTN_H);
        // ✅ 标语固定贴中间区块右边缘，永远不跟着标题跑！
        int sloganX = textRight - SLOGAN_W;
        slogan_preview->resize(sloganX, previewTitleY, SLOGAN_W, BTN_H);

        int previewBoxY = previewTitleY + BTN_H + GAP;
        const int MIN_DRAFT_H = 110;

        int draftBottomY = winH - GAP;
        const int TITLE_BAR_H = BTN_H;
        int draftBoxTopSpace = MID_GAP + TITLE_BAR_H;
        int previewAvailH = draftBottomY - previewBoxY - draftBoxTopSpace;

        int previewH = (int)(previewAvailH * 0.70);
        int draftH = previewAvailH - previewH;
        if (draftH < MIN_DRAFT_H)
        {
            draftH = MIN_DRAFT_H;
            previewH = previewAvailH - draftH;
        }

        disp_view->resize(textLeft, previewBoxY, textRight - textLeft, previewH);

        // ===================== 对齐左右树和标签Browser，顶部对齐previewBoxY =====================
        int newTreeH = leftScrollBottomY - previewBoxY;
        if (newTreeH < 40)
            newTreeH = 40;
        tree_nav->resize(tree_nav->x(), previewBoxY, tree_nav->w(), newTreeH);

        int newTagH = rightScrollBottomY - previewBoxY;
        if (newTagH < 20)
            newTagH = 20;
        browser_tag->resize(browser_tag->x(), previewBoxY, browser_tag->w(), newTagH);
        // ========================================================================================

        int draftTitleBarY = previewBoxY + previewH + MID_GAP;

        // ========== 编辑标题行 ==========
        title_draft->resize(textLeft, draftTitleBarY, labelFixedW, BTN_H);
        title_draft->labelfont(FL_HELVETICA_BOLD);
        title_draft->labelsize(18);
        title_draft->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CENTER);

        int midInputX = textLeft + labelFixedW + (midAvailW - ctrlW) / 2 + OFFSET;
        title_input->resize(midInputX, draftTitleBarY, ctrlW, BTN_H);
        // 复用同一个sloganX，上下标语对齐，位置钉死在右侧
        slogan_draft->resize(sloganX, draftTitleBarY, SLOGAN_W, BTN_H);

        int draftBoxY = draftTitleBarY + TITLE_BAR_H + GAP;
        edit_draft->resize(textLeft, draftBoxY, textRight - textLeft, draftH);
    }
};

// ===================== 程序入口 main函数【C++唯一入口】 =====================
int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    Fl::scheme("gtk+");
    Fl::background2(252, 250, 240);
    MyWindow *win = new MyWindow(WINDOW_W, WINDOW_H, "StrongerNotebook 强者记事簿");
    win->begin();
    Fl_Color COL_WIN_BG = fl_rgb_color(235, 233, 225);
    Fl_Color COL_PANEL = fl_rgb_color(252, 250, 240);
    Fl_Color COL_BTN_NORM = fl_rgb_color(220, 218, 210);
    Fl_Color COL_ACCENT = fl_rgb_color(149, 75, 55);
    Fl_Color COL_ACCENT_TEXT = fl_rgb_color(236, 231, 215);

    win->color(COL_WIN_BG);

    title_tree = new Fl_Box(GAP, GAP, TREE_W - GAP * 2, TITLE_H, "目录");
    title_tree->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title_tree->box(FL_NO_BOX);

    tree_nav = new Fl_Tree(GAP, GAP + TITLE_H + GAP, TREE_W - GAP * 2, 200);
    tree_nav->callback(cb_tree_click);
    tree_nav->color(COL_PANEL);
    tree_nav->box(FL_DOWN_BOX);
    tree_nav->selection_color(COL_ACCENT);
    
    // ========== 【目录树往左收紧、隐藏ROOT】 ==========
    tree_nav->showroot(0);                  // 隐藏ROOT根节点
    tree_nav->marginleft(0);                // 整体左留白压到2px
    tree_nav->labelmarginleft(0);           // 文字紧贴折叠图标
    tree_nav->item_labelsize(12);

    //===左侧4个按钮：新建今日日志、追加复盘、恢复目录树、检验&重建索引
    btn_new_today = new Fl_Button(GAP, 0, TREE_W - GAP * 2, BTN_H, "新建今日日志");
    btn_new_today->callback(cb_new_today_log);
    btn_new_today->color(COL_BTN_NORM);
    btn_new_today->labelcolor(FL_BLACK);

    btn_append_review = new Fl_Button(GAP, 0, TREE_W - GAP * 2, BTN_H, "追加复盘");
    btn_append_review->callback(cb_append_review);
    btn_append_review->color(COL_BTN_NORM);
    btn_append_review->labelcolor(FL_BLACK);

    btn_restore = new Fl_Button(GAP, 0, TREE_W - GAP * 2, BTN_H, "恢复目录树");
    btn_restore->callback(cb_restore_tree);
    btn_restore->color(COL_BTN_NORM);
    btn_restore->labelcolor(FL_BLACK);

    btn_refresh = new Fl_Button(GAP, 0, TREE_W - GAP * 2, BTN_H, "检验&重建索引");
    btn_refresh->callback(cb_refresh_index);
    btn_refresh->color(COL_BTN_NORM);
    btn_refresh->labelcolor(FL_BLACK);

    // 阅览区静态标签（只保留文字：阅览区，靠左）
    title_preview = new Fl_Box(0, 0, 100, BTN_H, "阅览区");
    title_preview->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title_preview->box(FL_NO_BOX);
    title_preview->labelfont(FL_HELVETICA_BOLD);
    title_preview->labelsize(18);

    // 【新增标语Box1】阅览行右侧标语
    slogan_preview = new Fl_Box(0, 0, 100, BTN_H, "旧·迹·既·定·仅·供·观·省");
    slogan_preview->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE); // 框内居中
    slogan_preview->box(FL_NO_BOX);
    slogan_preview->labelfont(FL_HELVETICA);
    slogan_preview->labelsize(18);
    slogan_preview->labelcolor(fl_rgb_color(102, 108, 112));

// ========== 预览区顶部，只读标题展示按钮 ==========
    preview_title_btn = new Fl_Button(0, 0, 100, BTN_H, "");
    preview_title_btn->color(COL_WIN_BG);
    preview_title_btn->box(FL_NO_BOX);       // 去掉所有边框、背景块，只渲染文字
    preview_title_btn->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE); // ✅ 文字居中
    // 空回调，点了啥也不干，但是控件是激活状态，label/redraw稳定
    preview_title_btn->callback([](Fl_Widget *, void *) {});
    preview_title_btn->labelfont(FL_HELVETICA_BOLD);     // 加粗标题
    preview_title_btn->labelsize(20);                   // 字号20
    preview_title_btn->labelcolor(fl_rgb_color(35,35,35));  // 深灰文字，不刺眼

    // 草稿区上方标签（只保留文字：编辑区，靠左）
    title_draft = new Fl_Box(0, 0, 100, BTN_H, "编辑区");
    title_draft->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title_draft->box(FL_NO_BOX);
    title_draft->labelfont(FL_HELVETICA_BOLD);
    title_draft->labelsize(18);

    // 【新增标语Box2】编辑行右侧标语
    slogan_draft = new Fl_Box(0, 0, 100, BTN_H, "落·笔·当·诚·直·面·本·心");
    slogan_draft->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE); // 框内居中
    slogan_draft->box(FL_NO_BOX);
    slogan_draft->labelfont(FL_HELVETICA);
    slogan_draft->labelsize(18);
    slogan_draft->labelcolor(fl_rgb_color(102, 108, 112));

    // ========== 标题输入框 title_input 配套字体配色 ==========
    title_input = new Fl_Input(0, 0, 100, BTN_H);
    title_input->textfont(FL_HELVETICA);
    title_input->textsize(20);                              // 和上面按钮字号统一
    title_input->textcolor(fl_rgb_color(30,30,30));
    title_input->selection_color(COL_ACCENT_TEXT);
    // =========================================================

    buf_preview = new Fl_Text_Buffer();
    disp_view = new Fl_Text_Display(0, 0, 100, 100);
    disp_view->buffer(buf_preview);
    disp_view->color(COL_PANEL);
    disp_view->box(FL_DOWN_BOX);
    disp_view->textsize(20);
    disp_view->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
    disp_view->selection_color(COL_ACCENT_TEXT); // ✅ 改成文本专用深色选中色

    buf_draft = new Fl_Text_Buffer();
    edit_draft = new Fl_Text_Editor(0, 0, 100, 100);
    edit_draft->buffer(buf_draft);
    edit_draft->color(COL_PANEL);
    edit_draft->box(FL_DOWN_BOX);
    edit_draft->textsize(20);
    edit_draft->selection_color(COL_ACCENT_TEXT); // ✅ 改成文本专用深色选中色
    edit_draft->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    title_tag = new Fl_Box(0, 0, TAG_W - GAP * 2, TITLE_H, "标签列表");
    title_tag->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    title_tag->box(FL_NO_BOX);

    browser_tag = new Fl_Hold_Browser(0, 0, TAG_W - GAP * 2, 100);
    browser_tag->callback(cb_browser_tag);
    browser_tag->textsize(20);
    // ==========统一暖棕红高亮方案 ==========
    browser_tag->color(COL_PANEL);
    browser_tag->textcolor(fl_rgb_color(55, 52, 48));
    browser_tag->selection_color(COL_ACCENT); // ✅ 标签列表保持原来的选中色不变
    browser_tag->labelcolor(fl_rgb_color(130, 126, 118));
    browser_tag->box(FL_DOWN_BOX);

    input_tag = new Fl_Input(0, 0, TAG_W - GAP * 2, BTN_H);
    input_tag->selection_color(COL_ACCENT_TEXT); // ✅ 标签输入框设置同一款深色选中色
    //====【改动】右侧标签按钮：合并为增修标签 + 删除标签
    btn_tag_upsert = new Fl_Button(0, 0, TAG_W - GAP * 2, BTN_H, "增修标签");
    btn_tag_upsert->callback(cb_tag_upsert);
    btn_tag_upsert->color(COL_BTN_NORM);

    btn_del_tag = new Fl_Button(0, 0, TAG_W - GAP * 2, BTN_H, "删除标签");
    btn_del_tag->callback(cb_tag_delete);
    btn_del_tag->color(COL_BTN_NORM);
    btn_del_tag->labelcolor(FL_BLACK);

    // ===================== 新增：搜索选中标签按钮 =====================
    // 交互约定：标签滚动区内的标签按钮点击只做选中标记，不会自动检索
    // 用户手动点击这个按钮，才根据当前选中的标签，反向筛选左侧目录树
    btn_tag_search = new Fl_Button(0, 0, TAG_W - GAP * 2, BTN_H, "搜索选中标签");
    btn_tag_search->callback(cb_tag_search);
    btn_tag_search->color(COL_BTN_NORM);
    btn_tag_search->labelcolor(FL_BLACK);

    btn_tag_show_all = new Fl_Button(0, 0, TAG_W - GAP * 2, BTN_H, "显示全部标签");
    btn_tag_show_all->callback(cb_tag_show_all);
    btn_tag_show_all->color(COL_BTN_NORM);
    btn_tag_show_all->labelcolor(FL_BLACK);

    win->end();
    win->resizable(win);
    win->size_range(MIN_W, MIN_H);
    win->callback(cb_win_close);
    if (!init_dir())
    {
        printf("[警告] LOGS目录或者index.json初始化失败\n");
    }
    else
    {
        printf("[OK] LOGS就绪，索引加载完成\n");
        render_tree();
        refresh_tag_panel();
    }
    win->show(argc, argv);
    return Fl::run();
}
