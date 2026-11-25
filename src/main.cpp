// Practice Replay - Geode mod for Geometry Dash (2.2091, Geode v6)
// Record in practice mode -> replay in normal mode -> export as .gdr

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Notification.hpp>

// Geode v6 returns asp::fs::Path from Mod::getSaveDir(); this header converts to std::filesystem.
#include <asp/fs/std.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace geode::prelude;

namespace {

constexpr int kFramerate = 240;

struct Input {
    int frame;
    int button;
    bool player2;
    bool down;
};

struct ReplayState {
    std::vector<Input> inputs;
    std::map<CheckpointObject*, int> checkpoints; // checkpoint -> frame it was created on

    bool recordArmed = false; // user pressed "Record"
    bool recording = false;   // actively recording right now
    bool playArmed = false;   // user pressed "Play"
    bool playing = false;     // actively playing back right now
    size_t cursor = 0;

    // Safe mode bookkeeping (replay runs are never saved)
    bool forcedTest = false;
    bool prevTest = false;

    int levelID = -1;
    std::string levelName;
};

ReplayState S;

// ---------------------------------------------------------------- files ---

// UTF-8 string for display (std::filesystem::path::string() can use the system codepage).
std::string pathToUtf8(std::filesystem::path const& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<char const*>(u.data()), u.size());
}

std::filesystem::path replayDir() {
    std::filesystem::path dir = Mod::get()->getSaveDir().toStdPath() / "replays";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string safeName(std::string const& in) {
    std::string out;
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') out += c;
        else out += '_';
    }
    if (out.empty()) out = "level";
    return out;
}

std::filesystem::path replayPath() {
    return replayDir() / (safeName(S.levelName) + "_" + std::to_string(S.levelID) + ".gdr");
}

std::string jsonEscape(std::string const& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\n";
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

// Builds a GDR (Geometry Dash Replay) JSON document.
std::string buildGdr() {
    double gameVersion = 2.2091;
    try { gameVersion = std::stod(Loader::get()->getGameVersion()); } catch (...) {}

    int lastFrame = S.inputs.empty() ? 0 : S.inputs.back().frame;
    double duration = static_cast<double>(lastFrame) / kFramerate;

    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "{";
    o << "\"gameVersion\":" << gameVersion << ",";
    o << "\"description\":\"Recorded with Practice Replay\",";
    o << "\"version\":1.0,";
    o << "\"duration\":" << duration << ",";
    o << "\"botInfo\":{\"name\":\"Practice Replay\",\"version\":\"1.0.0\"},";
    o << "\"levelInfo\":{\"id\":" << S.levelID << ",\"name\":\"" << jsonEscape(S.levelName) << "\"},";
    o << "\"framerate\":" << kFramerate << ",";
    o << "\"seed\":0,";
    o << "\"coins\":0,";
    o << "\"ldm\":false,";
    o << "\"inputs\":[";
    for (size_t i = 0; i < S.inputs.size(); i++) {
        auto const& in = S.inputs[i];
        if (i) o << ",";
        o << "{\"frame\":" << in.frame
          << ",\"btn\":" << in.button
          << ",\"2p\":" << (in.player2 ? "true" : "false")
          << ",\"down\":" << (in.down ? "true" : "false") << "}";
    }
    o << "]}";
    return o.str();
}

bool saveReplay(std::filesystem::path const& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto data = buildGdr();
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return f.good();
}

struct ParseResult {
    bool ok = false;
    std::string error;
    std::vector<Input> inputs;
    bool hasLevel = false;
    int levelId = 0;
    int framerate = kFramerate;
};

// Parses a JSON-style GDR document. Binary GDR files are rejected with a clear message.
ParseResult parseGdr(std::string const& text) {
    ParseResult r;

    size_t first = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (first == std::string::npos || text[first] != '{') {
        r.error = "This looks like a binary .gdr file. Only JSON-style .gdr files can be imported right now.";
        return r;
    }

    static std::regex const reInput(
        R"RX("frame"\s*:\s*(\d+)\s*,\s*"btn"\s*:\s*(\d+)\s*,\s*"2p"\s*:\s*(true|false)\s*,\s*"down"\s*:\s*(true|false))RX");
    static std::regex const reLevel(R"RX("levelInfo"\s*:\s*\{[^}]*?"id"\s*:\s*(-?\d+))RX");
    static std::regex const reRate(R"RX("framerate"\s*:\s*(\d+(?:\.\d+)?))RX");

    std::smatch m;
    if (std::regex_search(text, m, reLevel)) {
        r.hasLevel = true;
        try { r.levelId = std::stoi(m[1].str()); } catch (...) { r.hasLevel = false; }
    }
    if (std::regex_search(text, m, reRate)) {
        try { r.framerate = static_cast<int>(std::lround(std::stod(m[1].str()))); } catch (...) {}
    }

    for (auto it = std::sregex_iterator(text.begin(), text.end(), reInput); it != std::sregex_iterator(); ++it) {
        auto const& mm = *it;
        int frame = 0, btn = 0;
        try {
            frame = std::stoi(mm[1].str());
            btn = std::stoi(mm[2].str());
        } catch (...) { continue; }
        if (frame < 0 || btn < 1 || btn > 3) continue;
        r.inputs.push_back({frame, btn, mm[3].str() == "true", mm[4].str() == "true"});
    }

    if (r.inputs.empty()) {
        r.error = "No inputs were found in this file.";
        return r;
    }
    std::stable_sort(r.inputs.begin(), r.inputs.end(), [](Input const& a, Input const& b) {
        return a.frame < b.frame;
    });
    r.ok = true;
    return r;
}

std::string readFile(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Loads this level's previously saved replay from disk into memory.
bool loadReplay() {
    auto text = readFile(replayPath());
    if (text.empty()) return false;
    auto r = parseGdr(text);
    if (!r.ok) return false;
    S.inputs = std::move(r.inputs);
    return true;
}

// --------------------------------------------------------------- status ---

std::string statusText() {
    if (S.recording) return fmt::format("Recording ({} inputs)", S.inputs.size());
    if (S.playing) return "Replaying (safe mode)";
    if (S.recordArmed) return "Record armed: restart level";
    if (S.playArmed) return "Play armed: restart level";
    return "Idle";
}

void refreshStatus() {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_uiLayer) return;
    auto label = typeinfo_cast<CCLabelBMFont*>(pl->m_uiLayer->getChildByID("status-label"_spr));
    if (!label) return;
    if (S.recording || S.playing) {
        label->setString(S.recording ? "REC" : "REPLAY - SAFE MODE");
        label->setVisible(true);
    } else {
        label->setVisible(false);
    }
}

void toast(std::string const& msg) {
    Notification::create(msg, NotificationIcon::Info, 2.5f)->show();
}

// -------------------------------------------------------------- actions ---

void actionRecord() {
    auto pl = PlayLayer::get();
    if (!pl) return;

    if (!pl->m_isPracticeMode) {
        toast("Enable practice mode first");
        return;
    }

    S.recordArmed = !S.recordArmed;
    S.playArmed = false;
    S.playing = false;
    S.recording = false;
    if (S.recordArmed) {
        S.inputs.clear();
        S.checkpoints.clear();
        toast("Recording armed. Remove checkpoints & restart the level");
    } else {
        toast("Recording disarmed");
    }
    refreshStatus();
}

void actionPlay() {
    auto pl = PlayLayer::get();
    if (!pl) return;

    if (pl->m_isPracticeMode) {
        toast("Playback is for normal mode. Turn practice mode off");
        return;
    }

    if (!S.playArmed) {
        if (S.inputs.empty() && !loadReplay()) {
            toast("No replay recorded for this level yet");
            return;
        }
        S.playArmed = true;
        S.recordArmed = false;
        S.recording = false;
        toast("Playback armed. Restart the level");
    } else {
        S.playArmed = false;
        S.playing = false;
        toast("Playback disarmed");
    }
    refreshStatus();
}

void actionImport() {
    auto pl = PlayLayer::get();
    if (!pl) return;

    auto dir = replayDir() / "import";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::filesystem::path newest;
    std::filesystem::file_time_type best{};
    bool found = false;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code ec2;
        if (!e.is_regular_file(ec2)) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".gdr") continue;
        auto t = e.last_write_time(ec2);
        if (ec2) continue;
        if (!found || t > best) {
            best = t;
            newest = e.path();
            found = true;
        }
    }

    if (!found) {
        FLAlertLayer::create(
            "Import",
            fmt::format("Put a .gdr file in this folder, then press Import again:\n{}", pathToUtf8(dir)),
            "OK"
        )->show();
        return;
    }

    auto r = parseGdr(readFile(newest));
    if (!r.ok) {
        FLAlertLayer::create("Import failed", r.error, "OK")->show();
        return;
    }
    if (r.framerate != kFramerate) {
        FLAlertLayer::create(
            "Import failed",
            fmt::format("This replay is {} TPS, but this mod only supports {} TPS.", r.framerate, kFramerate),
            "OK"
        )->show();
        return;
    }
    if (r.hasLevel && r.levelId != S.levelID) {
        FLAlertLayer::create(
            "Wrong level",
            fmt::format("This replay is for level ID {}, but you are in level ID {}.", r.levelId, S.levelID),
            "OK"
        )->show();
        return;
    }

    S.inputs = std::move(r.inputs);
    S.checkpoints.clear();
    S.recordArmed = S.recording = false;
    S.playArmed = S.playing = false;
    FLAlertLayer::create(
        "Imported",
        fmt::format("Loaded {} inputs from:\n{}\n\nTurn practice mode off, press Play, then restart the level.",
                    S.inputs.size(), pathToUtf8(newest)),
        "OK"
    )->show();
    refreshStatus();
}

void actionExport() {
    if (S.inputs.empty()) {
        FLAlertLayer::create("Export", "There is nothing recorded yet.", "OK")->show();
        return;
    }
    auto path = replayPath();
    if (saveReplay(path)) {
        FLAlertLayer::create(
            "Exported",
            fmt::format("Saved GDR replay ({} inputs) to:\n{}", S.inputs.size(), pathToUtf8(path)),
            "OK"
        )->show();
    } else {
        FLAlertLayer::create("Export failed", "Could not write the replay file.", "OK")->show();
    }
}

} // namespace

// ------------------------------------------------------------ PlayLayer ---

class $modify(RPPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        auto winSize = CCDirector::get()->getWinSize();
        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("status-label"_spr);
        label->setScale(0.45f);
        label->setOpacity(160);
        label->setAnchorPoint({0.f, 1.f});
        label->setPosition({8.f, winSize.height - 8.f});
        label->setVisible(false);
        m_uiLayer->addChild(label, 100);
        refreshStatus();
    }

    CheckpointObject* createCheckpoint() {
        auto cp = PlayLayer::createCheckpoint();
        if (cp && S.recording) {
            S.checkpoints[cp] = m_gameState.m_currentProgress;
        }
        return cp;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        int id = m_level->m_levelID.value();
        std::string name = std::string(m_level->m_levelName);

        // New level -> forget everything about the old one.
        if (id != S.levelID || name != S.levelName) {
            S.levelID = id;
            S.levelName = name;
            S.inputs.clear();
            S.checkpoints.clear();
            S.recordArmed = S.playArmed = false;
            S.recording = S.playing = false;
        }

        // Where did this reset send us? 0 = start of level, >0 = a checkpoint frame, <0 = unknown
        int target = 0;
        if (m_isPracticeMode && m_checkpointArray && m_checkpointArray->count() > 0) {
            auto cp = static_cast<CheckpointObject*>(m_checkpointArray->lastObject());
            auto it = S.checkpoints.find(cp);
            target = (it != S.checkpoints.end()) ? it->second : -1;
        }

        S.cursor = 0;

        if (!m_isPracticeMode && S.playArmed && !S.inputs.empty()) {
            // ---- start playback (normal mode)
            S.playing = true;
            S.recording = false;
        } else {
            S.playing = false;

            if (!m_isPracticeMode) {
                S.recording = false; // left practice mode -> stop recording
            } else if (S.recordArmed && target == 0) {
                // ---- fresh run from the start: (re)start recording
                S.recording = true;
                S.inputs.clear();
                S.checkpoints.clear();
            } else if (S.recording && target > 0) {
                // ---- died & respawned at a checkpoint: drop everything after it
                std::erase_if(S.inputs, [target](Input const& i) { return i.frame >= target; });
            }
        }

        // ---- SAFE MODE (automatic, always on) ----
        // A replay-driven attempt is treated like a test run: nothing gets saved.
        if (S.playing && !S.forcedTest) {
            S.prevTest = m_isTestMode;
            m_isTestMode = true;
            S.forcedTest = true;
        } else if (!S.playing && S.forcedTest) {
            m_isTestMode = S.prevTest;
            S.forcedTest = false;
        }

        refreshStatus();
    }

    void levelComplete() {
        bool wasRecording = S.recording;
        bool wasPlaying = S.playing;
        PlayLayer::levelComplete();

        if (wasPlaying) {
            toast("Safe mode: replay runs are not saved");
        }

        if (wasRecording && !S.inputs.empty()) {
            S.recording = false;
            if (saveReplay(replayPath())) {
                toast("Replay auto-saved");
            }
        }
        refreshStatus();
    }

    void onQuit() {
        S.forcedTest = false;
        S.recording = S.playing = false;
        S.recordArmed = S.playArmed = false;
        PlayLayer::onQuit();
    }
};

// ------------------------------------------------------- GJBaseGameLayer ---

class $modify(RPBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (this == PlayLayer::get()) {
            // While a replay is running, ignore the real player's input.
            if (S.playing) return;

            if (S.recording) {
                S.inputs.push_back({
                    m_gameState.m_currentProgress,
                    button,
                    !isPlayer1,
                    down
                });
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (S.playing && this == PlayLayer::get()) {
            int frame = m_gameState.m_currentProgress;
            while (S.cursor < S.inputs.size() && S.inputs[S.cursor].frame <= frame) {
                auto const& in = S.inputs[S.cursor++];
                // Calls the ORIGINAL handler, so our own hook's "ignore input" check is skipped.
                GJBaseGameLayer::handleButton(in.down, in.button, !in.player2);
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

// ----------------------------------------------------------- Replay menu ---

class ReplayMenu : public FLAlertLayer {
protected:
    CCLabelBMFont* m_status = nullptr;

    bool init() {
        if (!FLAlertLayer::init(150)) return false;

        m_noElasticity = true;
        auto win = CCDirector::get()->getWinSize();
        CCPoint center = {win.width / 2.f, win.height / 2.f};

        m_mainLayer = CCLayer::create();
        this->addChild(m_mainLayer);

        auto bg = CCScale9Sprite::create("GJ_square01.png", {0.f, 0.f, 80.f, 80.f});
        bg->setContentSize({260.f, 270.f});
        bg->setPosition(center);
        m_mainLayer->addChild(bg, -1);

        auto title = CCLabelBMFont::create("Practice Replay", "goldFont.fnt");
        title->setScale(0.8f);
        title->setPosition({center.x, center.y + 115.f});
        m_mainLayer->addChild(title);

        m_status = CCLabelBMFont::create(statusText().c_str(), "bigFont.fnt");
        m_status->setScale(0.4f);
        m_status->setPosition({center.x, center.y + 88.f});
        m_mainLayer->addChild(m_status);

        m_buttonMenu = CCMenu::create();
        m_buttonMenu->setPosition(center);
        m_mainLayer->addChild(m_buttonMenu);

        auto add = [this](char const* text, SEL_MenuHandler sel, float y, float scale) {
            auto spr = ButtonSprite::create(text, 120, true, "bigFont.fnt", "GJ_button_01.png", 30.f, scale);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            btn->setPosition({0.f, y});
            m_buttonMenu->addChild(btn);
        };
        add("Record", menu_selector(ReplayMenu::onRecord), 52.f, 0.7f);
        add("Play", menu_selector(ReplayMenu::onPlay), 12.f, 0.7f);
        add("Import", menu_selector(ReplayMenu::onImport), -28.f, 0.7f);
        add("Export", menu_selector(ReplayMenu::onExport), -68.f, 0.7f);
        add("Close", menu_selector(ReplayMenu::onCloseBtn), -105.f, 0.55f);

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        return true;
    }

    void refresh() {
        if (m_status) m_status->setString(statusText().c_str());
    }

    void onRecord(CCObject*) { actionRecord(); refresh(); }
    void onPlay(CCObject*) { actionPlay(); refresh(); }
    void onImport(CCObject*) { actionImport(); refresh(); }
    void onExport(CCObject*) { actionExport(); refresh(); }

    void onCloseBtn(CCObject*) {
        this->setKeypadEnabled(false);
        this->removeFromParentAndCleanup(true);
    }

    void keyBackClicked() override { this->onCloseBtn(nullptr); }

public:
    static ReplayMenu* create() {
        auto ret = new ReplayMenu();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// ----------------------------------------------------------- PauseLayer ---

class $modify(RPPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("replay-menu"_spr);
        menu->setPosition({0.f, 0.f});

        auto spr = ButtonSprite::create("Replay", 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(RPPauseLayer::onRPOpen));
        btn->setID("replay-button"_spr);
        btn->setPosition({70.f, winSize.height / 2.f});
        menu->addChild(btn);

        this->addChild(menu);
    }

    void onRPOpen(CCObject*) {
        if (auto popup = ReplayMenu::create()) popup->show();
    }
};
