#include "raylib.h"
#include <fstream>
#include <string>
#include <vector>
#include <math.h>
#include <algorithm>
#include <iostream>
#include "../external/json.hpp"

bool DEBUG_MODE = false;

const std::string RESOURCE_PATH = "./resources/";

const std::string LAYER_ALWAYSABOVE = "AlwaysAbove";
const std::string LAYER_DRAWABLES = "Drawables";
const std::string LAYER_WORLDOBJECTS = "WorldObjects";
const std::string LAYER_TRANSITIONS = "Transitions";
const std::string LAYER_SPAWNPOINTS = "SpawnPoints";
const std::string LAYER_DIALOGUES = "Dialogues";
const std::string LAYER_EVENTS = "Events";

const int tileSize = 32;

const int GAME_WIDTH  = 1280;
const int GAME_HEIGHT = 720;

const int DOWN = 10;
const int UP = 8;
const int RIGHT = 11;
const int LEFT = 9;

enum GameState {
    STATE_NORMAL, STATE_TRANSITION, STATE_DIALOGUE, STATE_EVENT
};

GameState gameState = STATE_NORMAL;

using json = nlohmann::json;

json loadJson(const std::string& path) {
    std::ifstream f(path);
    json j;
    f >> j;
    return j;
}

struct Drawable {
    Rectangle src;
    Rectangle dst;
    Texture2D* texture;
    float sortY;

    int x, y;
    std::string layer;
};

struct Transition {
    Rectangle trigger;
    std::string map, spawnName;
};

struct NPC;

enum EventActionType {
    ACTION_MOVE_NPC, ACTION_MOVE_PLAYER, ACTION_MOVE_CAMERA, ACTION_DIALOGUE, ACTION_GROUP
};

struct EventAction {
    EventActionType type;
    bool started = false;
    float target;

    // Moves
    NPC* npc;        // Only in ACTION_MOVE_NPC
    int tiles;
    int direction;
    bool follow;     // Only in ACTION_MOVE_NPC and ACTION_MOVE_PLAYER ---- Camera follow

    // Dialogues
    std::string dialogue;   // Only in ACTION_DIALOGUE

    // Groups
    std::vector<EventAction> subactions; // Only in ACTION_GROUP
    bool finished = false;               // Only in ACTION_GROUP

    // Camera
    float speed;                         // Only in ACTION_MOVE_CAMERA
};

struct Event {
    std::string name;
    std::vector<EventAction> actions;
    int eventIndex = 0;
    bool triggered = false;
};

struct Dialogue {
    std::string name;
    std::vector<std::string> speaker, msg;
};

struct NPC {
    std::string name;

    float x, y;
    Rectangle body;
    Texture2D texture;
    float spriteW;
    float spriteH;

    int frame = 0;
    float frameTimer = 0.0f;
    float frameMaxTimer = 0.10f;
    int direction = DOWN;
    int default_direction = DOWN;

    float speed = 150.0f;

    Dialogue dialogue;
    bool hasDialogue = false;

    NPC() { }

    void buildNpc(std::string& frame_, std::string& name_, float& x_, float& y_) {
        name = name_;
        x = x_;
        y = y_;

        direction = loadFrame(frame_);
        default_direction = direction;

        Image image = LoadImage((RESOURCE_PATH + name + ".png").c_str());
        texture = LoadTextureFromImage(image);
        UnloadImage(image);

        spriteW = (float)texture.width / 13;
        spriteH = (float)texture.height / 54;

        updateBody();
    }

    void buildNpc(std::string& frame_, std::string& name_, float& x_, float& y_, Dialogue& d) {
        buildNpc(frame_, name_, x_, y_);
        dialogue = d;
        hasDialogue = true;
    }

    int loadFrame(std::string frame_) {
        if (frame_ == "FRAME_UP") return UP;
        else if (frame_ == "FRAME_RIGHT") return RIGHT;
        else if (frame_ == "FRAME_LEFT") return LEFT;
        //if (frame_ == "FRAME_DOWN") return DOWN;
        return DOWN;
    }

    void updateBody() {
        body = Rectangle{x + 20.0f, y + 17.0f, 24.0f, 16.0f};
    }

    void updateDirection(int dir) {
        switch (dir) {
            case DOWN:
                direction = UP;
                break;
            case UP:
                direction = DOWN;
                break;
            case RIGHT:
                direction = LEFT;
                break;
            case LEFT:
                direction = RIGHT;
                break;
        }
    }

    void updateFrame(float frameTime) {
        frameTimer += frameTime;

        if (frameTimer >= frameMaxTimer) {
            frameTimer -= frameMaxTimer;
            frame = (frame + 1) % 9;            // This 9 represents 9 frames in walking animation, should be changed if more animations are needed
        }
    }
};

struct Player {
    //Player Pos
    float x, y;
    Rectangle body;
    Texture2D texture;
    float spriteW;
    float spriteH;
    const float PLAYER_MARGIN = 1.0f;

    int frame = 0;
    //int frameCount = 4;
    float frameTimer = 0.0f;
    float frameMaxTimer = 0.10f;
    
    int direction = DOWN;

    float speed = 150.0f;

    int lastKey = 0;

    bool fading = false;
    float fadeAlpha = 0.0f;
    Transition* pendingTransition = nullptr;

    Event* ongoingEvent = nullptr;
    
    Dialogue* currentDialogue = nullptr;
    NPC* currentDialogueNPC = nullptr;
    int dialogueIndex = 0;
    int visibleChars = 0;
    float textTimer = 0.0f;
    float textSpeed = 0.03f;
    bool lineFinished = false;


    Player() {
        Image image = LoadImage((RESOURCE_PATH + "character-spritesheet.png").c_str());
        texture = LoadTextureFromImage(image);
        UnloadImage(image);

        spriteW = (float)texture.width / 13;
        spriteH = (float)texture.height / 54;

        updatePlayerBody();
    }

    ~Player() {
        UnloadTexture(texture);
    }

    void updatePlayerBody() {
        //Regular body
        //body = Rectangle{x + PLAYER_MARGIN, y + PLAYER_MARGIN, tileSize - PLAYER_MARGIN * 2, tileSize - PLAYER_MARGIN * 2};

        //Feet collision
        body = Rectangle{x + 22.0f, y + 25.0f, 20.0f, 8.0f};
    }

    void updatePlayerFrame(float frameTime) {
        frameTimer += frameTime;

        if (frameTimer >= frameMaxTimer) {
            frameTimer -= frameMaxTimer;
            frame = (frame + 1) % 9;            // This 9 represents 9 frames in walking animation, should be changed if more animations are needed
        }
    }

    void updatePlayerAnimation(float frameTime, float dx, float dy, int lastKey) {
        if (dx == 0 && dy == 0) {
            frame = 0;
            frameTimer = 0;
            return;
        }
        
        if (IsKeyUp(lastKey))
            switch (lastKey) { 
                case KEY_DOWN:  //264
                    if (dx != 0)
                        direction = (dx > 0) ? RIGHT : LEFT;
                    else direction = UP;
                    break;
                case KEY_UP:    //265
                    if (dx != 0)
                        direction = (dx > 0) ? RIGHT : LEFT;
                    else direction = DOWN;
                    break;
                case KEY_RIGHT: //262
                    if (dy != 0)
                        direction = (dy > 0) ? DOWN : UP;
                    else direction = LEFT;
                    break;
                case KEY_LEFT:  //263
                    if (dy != 0)
                        direction = (dy > 0) ? DOWN : UP;
                    else direction = RIGHT;
                    break;
            }
        else
            switch (lastKey) { 
                case KEY_DOWN:  //264
                    direction = DOWN;
                    break;
                case KEY_UP:    //265
                    direction = UP;
                    break;
                case KEY_RIGHT: //262
                    direction = RIGHT;
                    break;
                case KEY_LEFT:  //263
                    direction = LEFT;
                    break;
            }

        updatePlayerFrame(frameTime);
    }

    Rectangle getInteractionZone() {
        float size = 10.0f;

        switch (direction) {
            case DOWN:
                return { body.x, body.y + body.height, body.width, size + 5.0f };
            case UP:
                return { body.x, body.y - size - 5.0f, body.width, size + 5.0f };
            case RIGHT:
                return { body.x + size, body.y, body.width, size };
            case LEFT:
                return { body.x - size, body.y, body.width, size };
        }
        return {};
    }
};

struct TileAnimationFrame {
    int tileId;
    int duration;
};

struct Tileset {
    Texture2D texture;
    int firstGid;
    int tileWidth;
    int tileHeight;
    int columns;

    std::map<int, std::vector<TileAnimationFrame>> animations;
};

struct TileLayer {
    std::string name;
    std::vector<int> data;
    int width;
    int height;
};

struct WorldObject {
    int x, y;           // Careful not to input floats in the editor
    int endX, endY, startX, startY;
    std::string layer;
};

struct SpawnPoint {
    std::string who, name, frame;
    float x, y;
    std::string dialogue = "";
};

struct DialoguePoint {
    Rectangle trigger;
    std::string src;
};

struct EventPoint {
    Rectangle trigger;
    std::string name;
};

struct Map {
    std::vector<TileLayer> layers;
    std::vector<Tileset> tilesets;
    std::vector<std::vector<int>> collisions;
    std::vector<Drawable> staticDrawables;
    std::vector<Drawable> dynamicDrawables;
    std::vector<WorldObject> worldObjects;
    std::vector<Transition> transitions;
    std::vector<SpawnPoint> spawnPoints;
    std::vector<DialoguePoint> dialoguePoints;
    std::vector<Dialogue> dialogues;
    std::vector<NPC> npcs;
    std::vector<EventPoint> eventPoints;
    std::vector<Event> events;

    std::string playerSpawnName;
    SpawnPoint playerSpawn;
    int height, width = 0;

    std::vector<Rectangle> debugColliders;

    Map() { }

    ~Map() {
        for (Tileset& ts : tilesets)
            UnloadTexture(ts.texture);
    }

    void loadMap(const std::string& filename, const std::string& spawn) {
        playerSpawnName = spawn;
        loadFromTMJ(RESOURCE_PATH + filename + ".tmj");
        loadCollisions((RESOURCE_PATH + filename + "_collisions.csv").c_str());
        loadStaticDrawables();
        loadDialogues((RESOURCE_PATH + filename + "_dialogues.json").c_str());
        loadNpcs();
        loadEvents((RESOURCE_PATH + filename + "_events.json").c_str());
    }

    void loadFromTMJ(const std::string& filename) {
        layers.clear();
        tilesets.clear();
        worldObjects.clear();
        transitions.clear();
        spawnPoints.clear();
        eventPoints.clear();

        json j = loadJson(filename);

        // Load tilesets
        for (json forTileset : j["tilesets"]) {
            json jsonTileset;
            // .tsj
            std::string imgPath;
            if (forTileset.contains("source")) {
                std::string tsjRelative = forTileset["source"];
                std::string tsjPath = RESOURCE_PATH + tsjRelative;
                jsonTileset = loadJson(tsjPath);
                std::string tsjFolder = tsjRelative.substr(0, tsjRelative.find_last_of("/\\") + 1);
                imgPath = RESOURCE_PATH + tsjFolder + jsonTileset["image"].get<std::string>();
            }
            // .png
            else {
                jsonTileset = forTileset;
                imgPath = RESOURCE_PATH + jsonTileset["image"].get<std::string>();
            }
                
            Texture2D tex = LoadTexture(imgPath.c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_POINT);

            Tileset tileset;
            tileset.texture = tex;
            tileset.firstGid   = forTileset["firstgid"].get<int>();
            tileset.tileWidth  = jsonTileset["tilewidth"].get<int>();
            tileset.tileHeight = jsonTileset["tileheight"].get<int>();
            tileset.columns    = jsonTileset["columns"].get<int>();

            // Animations
            if (jsonTileset.contains("tiles")) {
                for (json tile : jsonTileset["tiles"]) {
                    if (!tile.contains("animation")) continue;

                    int tileId = tile["id"].get<int>();

                    for (json frame : tile["animation"]) {
                        tileset.animations[tileId].push_back({
                            frame["tileid"].get<int>(),
                            frame["duration"].get<int>()
                        });
                    }
                }
            }

            tilesets.push_back(tileset);
        }

        // Load layers
        for (json layer : j["layers"]) {
            if (layer["type"] == "tilelayer") {
                if (layer["name"] == "Collisions") continue;        //Ignore collisions, they are parsed separately

                TileLayer tlayer;
                tlayer.name   = layer["name"].get<std::string>();
                tlayer.width  = layer["width"].get<int>();
                tlayer.height = layer["height"].get<int>();
                tlayer.data   = layer["data"].get<std::vector<int>>();

                layers.push_back(tlayer);
            }
            else if (layer["type"] == "objectgroup") {

                if (layer["name"] == LAYER_WORLDOBJECTS) {
                    for (json obj : layer["objects"]) {
                        if (!obj.contains("properties")) continue;
                        WorldObject wo;
                        for (json property : obj["properties"]) {
                            if (property["name"] == "endX")
                                wo.endX = property["value"].get<int>();
                            else if (property["name"] == "endY")
                                wo.endY = property["value"].get<int>();
                            else if (property["name"] == "layer")
                                wo.layer = property["value"].get<std::string>();
                            else if (property["name"] == "startX")
                                wo.startX = property["value"].get<int>();
                            else if (property["name"] == "startY")
                                wo.startY = property["value"].get<int>();
                        }
                        wo.x = obj["x"].get<int>() / 16;
                        wo.y = obj["y"].get<int>() / 16;
                        worldObjects.push_back(wo);
                    }
                }

                else if (layer["name"] == LAYER_TRANSITIONS) {
                    for (json obj : layer["objects"]) {
                        if (!obj.contains("properties")) continue;
                        Transition t;
                        for (json property : obj["properties"]) {
                            if (property["name"] == "map")
                                t.map = property["value"].get<std::string>();
                            else if (property["name"] == "spawnName")
                                t.spawnName = property["value"].get<std::string>();
                        }
                        t.trigger = {
                            obj["x"].get<float>() * 2.0f,           // Go from 16px tiles to 32px tiles
                            obj["y"].get<float>() * 2.0f,
                            obj["width"].get<float>() * 2.0f,
                            obj["height"].get<float>() * 2.0f
                        };
                        transitions.push_back(t);
                    }
                }

                else if (layer["name"] == LAYER_SPAWNPOINTS) {
                    for (json obj : layer["objects"]) {
                        if (!obj.contains("properties")) continue;
                        SpawnPoint sp;
                        for (json property : obj["properties"]) {
                            if (property["name"] == "who")
                                sp.who = property["value"].get<std::string>();
                            else if (property["name"] == "name")
                                sp.name = property["value"].get<std::string>();
                            else if (property["name"] == "frame")
                                sp.frame = property["value"].get<std::string>();
                            else if (property["name"] == "dialogue")
                                sp.dialogue = property["value"].get<std::string>();
                        }
                        sp.x = obj["x"].get<float>() * 2.0f;
                        sp.y = obj["y"].get<float>() * 2.0f;
                        sp.x -= 16.0f;                          //Small offset accounting for ~16 blank pixels on most spritesheets
                        if (sp.who == "player") {
                            if (sp.name == playerSpawnName)
                                playerSpawn = sp;
                            continue;
                        }
                        spawnPoints.push_back(sp);
                    }
                }

                else if (layer["name"] == LAYER_DIALOGUES) {
                    for (json obj : layer["objects"]) {
                        if (!obj.contains("properties")) continue;
                        DialoguePoint dp;
                        for (json property : obj["properties"]) {
                            if (property["name"] == "src")
                                dp.src = property["value"].get<std::string>();
                        }
                        dp.trigger = {
                            obj["x"].get<float>() * 2.0f,           // Go from 16px tiles to 32px tiles
                            obj["y"].get<float>() * 2.0f,
                            obj["width"].get<float>() * 2.0f,
                            obj["height"].get<float>() * 2.0f
                        };
                        dialoguePoints.push_back(dp);
                    }
                }

                else if (layer["name"] == LAYER_EVENTS) {
                    for (json obj : layer["objects"]) {
                        if (!obj.contains("properties")) continue;
                        EventPoint ep;
                        for (json property : obj["properties"]) {
                            if (property["name"] == "name")
                                ep.name = property["value"].get<std::string>();
                        }
                        ep.trigger = {
                            obj["x"].get<float>() * 2.0f,           // Go from 16px tiles to 32px tiles
                            obj["y"].get<float>() * 2.0f,
                            obj["width"].get<float>() * 2.0f,
                            obj["height"].get<float>() * 2.0f
                        };
                        eventPoints.push_back(ep);
                    }
                }
            }
        }

        if (!layers.empty()) {
            width = layers[0].width;
            height = layers[0].height;
        }
    }

    void loadCollisions(const char* filename) {
        collisions.clear();

        std::ifstream file(filename);
        std::string line;

        while (std::getline(file, line)) {
            std::vector<int> row;
            std::string cell;

            for (char c : line) {
                if (c == ',') {
                    row.push_back(std::stoi(cell));
                    cell.clear();
                    continue;
                } 
                cell += c;
            }
            row.push_back(std::stoi(cell));
            collisions.push_back(row);
        }
    }

    void loadStaticDrawables() {
        staticDrawables.clear();
        for (TileLayer& layer : layers) {
            if (layer.name != LAYER_DRAWABLES) continue;

            for (int y = 0; y < layer.height; y++) {
                for (int x = 0; x < layer.width; x++) {
                    int gid = layer.data[y * layer.width + x];

                    if (gid <= 0) continue;

                    Tileset* ts = findTileset(gid);
                    if (!ts) continue;

                    int localId = gid - ts->firstGid;

                    Rectangle src = {
                        (float)((localId % ts->columns) * ts->tileWidth),
                        (float)((localId / ts->columns) * ts->tileHeight),
                        (float)ts->tileWidth,
                        (float)ts->tileHeight
                    };

                    Rectangle dst = {
                        (float)(x * tileSize),
                        (float)(y * tileSize),
                        (float)tileSize,
                        (float)tileSize
                    };

                    Drawable d;
                    d.texture = &ts->texture;
                    d.src = src;
                    d.dst = dst;
                    d.sortY = dst.y + dst.height;

                    d.x = x;
                    d.y = y;
                    d.layer = LAYER_DRAWABLES;
                    staticDrawables.push_back(d);
                }
            }
        }

        // Update sortY to match object anchor
        for (WorldObject wo : worldObjects) {
            int anchor = -1;
            for (Drawable& dr : staticDrawables) {
                if ((dr.x == wo.x) && (dr.y == wo.y) && (dr.layer == wo.layer)) {
                    anchor = dr.sortY;
                    break;
                }
            }
            if (anchor == -1) continue;
            for (int x = wo.startX+wo.x; x < wo.endX+wo.x+1; x++)
                for (int y = wo.startY+wo.y; y < wo.endY+wo.y+1; y++)
                    for (Drawable& dr : staticDrawables) {
                        if ((dr.x == x) && (dr.y == y) && (dr.layer == wo.layer)) {
                            dr.sortY = anchor;
                            break;
                        }
                    }
        }
    }

    void loadDialogues(const char* filename) {
        dialogues.clear();

        json j = loadJson(filename);
        
        for (json d : j["dialogues"]) {
            Dialogue dia;
            dia.name = d["name"].get<std::string>();
            for (json s : d["sentences"]) {
                dia.speaker.push_back(s["speaker"].get<std::string>());
                dia.msg.push_back(s["msg"].get<std::string>());
            }
            dialogues.push_back(dia);
        }
    }

    void loadNpcs() {
        npcs.clear();
        for (SpawnPoint& sp : spawnPoints) {
            if (sp.who != "npc")
                continue;
            if (sp.dialogue != "") {
                for (Dialogue& dia : dialogues) {
                    if (sp.dialogue == dia.name) {
                        npcs.emplace_back();
                        npcs.back().buildNpc(sp.frame, sp.name, sp.x, sp.y, dia);
                    }
                }
            }
            else {
                npcs.emplace_back();
                npcs.back().buildNpc(sp.frame, sp.name, sp.x, sp.y);
            }
        }
    }

    EventAction parseAction(const json& a) {
        EventAction action;
        std::string str_type = a["type"].get<std::string>();
        EventActionType type = ACTION_DIALOGUE;                     // Should never keep this, but compiler will throw warning if it's not there
        if (str_type == "ACTION_DIALOGUE") type = ACTION_DIALOGUE;
        if (str_type == "ACTION_MOVE_NPC") type = ACTION_MOVE_NPC;
        if (str_type == "ACTION_MOVE_CAMERA") type = ACTION_MOVE_CAMERA;
        if (str_type == "ACTION_MOVE_PLAYER") type = ACTION_MOVE_PLAYER;
        if (str_type == "ACTION_GROUP") type = ACTION_GROUP;
        action.type = type;
        if ((type == ACTION_MOVE_CAMERA) || (type == ACTION_MOVE_NPC) || (type == ACTION_MOVE_PLAYER)) {
            action.tiles = a["tiles"].get<int>();
            std::string str_direction = a["direction"].get<std::string>();
            int direction = RIGHT;                                              // Again, should never keep this value
            if (str_direction == "RIGHT") direction = RIGHT;
            if (str_direction == "LEFT") direction = LEFT;
            if (str_direction == "UP") direction = UP;
            if (str_direction == "DOWN") direction = DOWN;
            action.direction = direction;
        }
        if (type == ACTION_MOVE_NPC) {
            std::string str_npc = a["npc"].get<std::string>();
            NPC* npc = nullptr;
            for (NPC& n : npcs)
                if (n.name == str_npc)
                    npc = &n;
            action.npc = npc;
            action.follow = a["follow"].get<bool>();
        }
        if (type == ACTION_MOVE_PLAYER)
            action.follow = a["follow"].get<bool>();
        if (type == ACTION_DIALOGUE)
            action.dialogue = a["dialogue"].get<std::string>();
        if (type == ACTION_MOVE_CAMERA)
            action.speed = a["speed"].get<float>();
        if (type == ACTION_GROUP)
            for (json sub : a["actions"])
                action.subactions.push_back(parseAction(sub));
        return action;
    }

    void loadEvents(const char* filename) {
        events.clear();

        json j = loadJson(filename);
        
        for (json e : j["events"]) {
            Event ev;
            ev.name = e["name"].get<std::string>();
            // TODO: GESTIÓN DE TRIGGERED CON FLAGS GLOBALES QUE PERSISTAN A TRANSICIONES DE MAPA
            for (json a : e["actions"]) {
                ev.actions.push_back(parseAction(a));
            }
            events.push_back(ev);
        }
    }

    int collisionValue(int tx, int ty) {
        if (tx < 0 || ty < 0 || ty >= height || tx >= width)
            return true; // Outside the map
        return collisions[ty][tx];
    }

    Rectangle getTileCollider(int tx, int ty, int col) {
        //0 DOWN, 1 UP, 2 RIGHT, 3 LEFT

        if (col < 32*1) {
            return Rectangle{
                tx * tileSize + 0.0f,
                ty * tileSize + col + 0.0f,
                tileSize,
                tileSize
            };
        }
        else if (col < 32*2) {
            col = col % 32;
            return Rectangle{
                tx * tileSize + 0.0f,
                ty * tileSize - col + 0.0f,
                tileSize,
                tileSize
            };
        }
        else if (col < 32*3) {
            col = col % 32;
            return Rectangle{
                tx * tileSize + col + 0.0f,
                ty * tileSize + 0.0f,
                tileSize,
                tileSize
            };
        }
        else if (col < 32*4) {
            col = col % 32;
            return Rectangle{
                tx * tileSize - col + 0.0f,
                ty * tileSize + 0.0f,
                tileSize,
                tileSize
            };
        }
        else return Rectangle {};       // Error, should never happen
    }

    bool checkCollision(Rectangle& playerBody) {
        return (checkMapCollision(playerBody) || checkNpcCollision(playerBody));
    }

    bool checkMapCollision(Rectangle& playerBody) {
        int left   = (int)floor(playerBody.x / tileSize - 1.0f);
        int right  = (int)floor((playerBody.x + playerBody.width) / tileSize + 1.0f);
        int top    = (int)floor(playerBody.y / tileSize - 1.0f);
        int bottom = (int)floor((playerBody.y + playerBody.height) / tileSize + 1.0f);

        for (int y = top; y <= bottom; y++) {
            for (int x = left; x <= right; x++) {
                
                int col = collisionValue(x, y);
                if (col == -1)
                    continue;

                Rectangle tileCol = getTileCollider(x, y, col);
                if (DEBUG_MODE) debugColliders.push_back(tileCol);      //DEBUG

                if (CheckCollisionRecs(playerBody, tileCol))
                    return true;
            }
        }
        return false;
    }

    bool checkNpcCollision(Rectangle& playerBody) {
        for (NPC npc : npcs)
            if (CheckCollisionRecs(playerBody, npc.body))
                return true;
        return false;
    }

    Tileset* findTileset(int gid) {
        for (int i = tilesets.size() - 1; i >= 0; --i) {
            if (gid >= tilesets[i].firstGid)
                return &tilesets[i];
        }
        return nullptr;
    }

    void drawMap(bool altitude) {               // True for normal layers, false for topmost layers
        for (TileLayer& layer : layers) {
            if (altitude && layer.name == LAYER_ALWAYSABOVE) continue;
            if (!altitude && layer.name != LAYER_ALWAYSABOVE) continue;

            for (int y = 0; y < layer.height; y++) {
                for (int x = 0; x < layer.width; x++) {
                    int gid = layer.data[y * layer.width + x];

                    if (gid <= 0) continue;

                    Tileset* ts = findTileset(gid);
                    if (!ts) continue;

                    int localId = gid - ts->firstGid;

                    auto it = ts->animations.find(localId);
                    if (it != ts->animations.end()) {
                        std::vector<TileAnimationFrame> anim = it->second;

                        int totalTime = 0;
                        for (TileAnimationFrame f : anim) totalTime += f.duration;

                        int t = (int)(GetTime() * 1000) % totalTime;

                        int acc = 0;
                        for (TileAnimationFrame f : anim) {
                            acc += f.duration;
                            if (t < acc) {
                                localId = f.tileId;
                                break;
                            }
                        }
                    }

                    Rectangle src = {
                        (float)((localId % ts->columns) * ts->tileWidth),
                        (float)((localId / ts->columns) * ts->tileHeight),
                        (float)ts->tileWidth,
                        (float)ts->tileHeight
                    };

                    Rectangle dst = {
                        (float)(x * tileSize),
                        (float)(y * tileSize),
                        (float)tileSize,
                        (float)tileSize
                    };

                    DrawTexturePro(ts->texture, src, dst, {0,0}, 0, WHITE);
                }
            }
        }
    }
};

Camera2D setupCamera(Player &player) {
    Camera2D camera = {0};
    camera.target = { player.x + tileSize/2.0f, player.y + tileSize/2.0f };
    camera.offset = { GAME_WIDTH/2.0f, GAME_HEIGHT/2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    return camera;
}

void initialize() {
    //Game Window
    InitWindow(0, 0, "Empyral Imperium");
    ToggleBorderlessWindowed();
    ClearWindowState(FLAG_WINDOW_TOPMOST);
    SetTargetFPS(60);
    HideCursor();
}

void input(Player &player, Map &map) {
    if (gameState == STATE_DIALOGUE) {
        if (IsKeyPressed(KEY_Z)) {
            if (!player.lineFinished) {
                player.visibleChars = player.currentDialogue->msg[player.dialogueIndex].size();
                player.lineFinished = true;
            } else {
                player.dialogueIndex++;

                if (player.dialogueIndex >= (int)player.currentDialogue->msg.size()) {
                    gameState = STATE_NORMAL;
                    player.currentDialogue = nullptr;
                    if (player.currentDialogueNPC != nullptr) {
                        player.currentDialogueNPC->direction = player.currentDialogueNPC->default_direction;
                        player.currentDialogueNPC = nullptr;
                    }
                } else {
                    player.visibleChars = 0;
                    player.textTimer = 0.0f;
                    player.lineFinished = false;
                }
            }
        }
        else if (IsKeyPressed(KEY_X)) {
            if (!player.lineFinished) {
                player.visibleChars = player.currentDialogue->msg[player.dialogueIndex].size();
                player.lineFinished = true;
            }
        }
        return;
    }

    if ((gameState == STATE_NORMAL) && IsKeyPressed(KEY_Z)) {
        // Object dialogues - could be rewritten to use interaction zone, not really used
        for (DialoguePoint &dp : map.dialoguePoints) {
            if (CheckCollisionRecs(player.body, dp.trigger)) {
                for (Dialogue& dia : map.dialogues) {
                    if (dp.src == dia.name) {
                        gameState = STATE_DIALOGUE;
                        player.currentDialogue = &dia;

                        player.frame = 0;

                        player.dialogueIndex = 0;
                        player.visibleChars = 0;
                        player.textTimer = 0.0f;
                        player.lineFinished = false;

                        return;
                    }
                }
            }
        }

        // NPC dialogues
        Rectangle interact = player.getInteractionZone();
        map.debugColliders.push_back(interact);

        for (NPC& npc : map.npcs) {
            if (npc.hasDialogue && CheckCollisionRecs(interact, npc.body)) {
                gameState = STATE_DIALOGUE;
                player.currentDialogue = &npc.dialogue;
                player.currentDialogueNPC = &npc;

                player.frame = 0;

                npc.updateDirection(player.direction);

                player.dialogueIndex = 0;
                player.visibleChars = 0;
                player.textTimer = 0.0f;
                player.lineFinished = false;

                return;
            }
        }
    }

    float dt = GetFrameTime();

    float dx = 0;
    float dy = 0;

    if (IsKeyDown(KEY_RIGHT)) dx += player.speed * dt;
    if (IsKeyDown(KEY_LEFT))  dx -= player.speed * dt;
    if (IsKeyDown(KEY_UP))    dy -= player.speed * dt;
    if (IsKeyDown(KEY_DOWN))  dy += player.speed * dt;

    int backupKey = GetKeyPressed();
    if ( (backupKey == KEY_RIGHT) || (backupKey == KEY_LEFT) || (backupKey == KEY_UP) || (backupKey == KEY_DOWN) )  
        player.lastKey = backupKey;

    if (IsKeyPressed(KEY_F1)) DEBUG_MODE = !DEBUG_MODE;

    if (gameState != STATE_NORMAL) return;        // Block controls while in transition or any other irregular state

    // Very basic running system
    if (IsKeyDown(KEY_LEFT_SHIFT)) {
        player.speed = 250.0f;
        player.frameMaxTimer = 0.08f;
    }
    else {
        player.speed = 150.0f;
        player.frameMaxTimer = 0.10f;
    }

    player.updatePlayerBody();

    if (dx != 0.0f) {
        Rectangle playerCopy = player.body;
        playerCopy.x += dx;

        if (!map.checkCollision(playerCopy))
            player.x += dx;
    }

    player.updatePlayerBody();

    if (dy != 0.0f) {
        Rectangle playerCopy = player.body;
        playerCopy.y += dy;

        if (!map.checkCollision(playerCopy))
            player.y += dy;
    }

    player.updatePlayerBody();
    player.updatePlayerAnimation(GetFrameTime(), dx, dy, player.lastKey);

    for (Transition& t : map.transitions) {
        if (CheckCollisionRecs(player.body, t.trigger)) {
            gameState = STATE_TRANSITION;
            player.fading = true;
            player.pendingTransition = &t;
            break;
        }
    }

    for (EventPoint& ep : map.eventPoints) {
        if (CheckCollisionRecs(player.body, ep.trigger)) {
            for (Event& ev : map.events) {
                if (ep.name == ev.name) {
                    if (ev.triggered) break;
                    player.ongoingEvent = &ev;
                    gameState = STATE_EVENT;
                }
            }
            break;
        }
    }
}

bool executeAction(EventAction& action, Player& player, Camera2D& camera) {
    switch (action.type) {
        case ACTION_DIALOGUE: {
            return true;
            break;
        }
        // case action pause + rewrite player/npc functions
        case ACTION_MOVE_CAMERA: {
            if (!action.started) {
                action.started = true;
                player.frame = 0;
                switch (action.direction) {
                    case RIGHT:
                        action.target = camera.target.x + action.tiles * tileSize;
                        break;
                    case LEFT:
                        action.target = camera.target.x - action.tiles * tileSize;
                        break;
                    case DOWN:
                        action.target = camera.target.y + action.tiles * tileSize;
                        break;
                    case UP:
                        action.target = camera.target.y - action.tiles * tileSize;
                        break;
                }
            }

            switch (action.direction) {
                case RIGHT:
                    camera.target.x = round(std::min(camera.target.x + action.speed * GetFrameTime(), action.target));
                    if (camera.target.x == action.target) return true;
                    break;
                case LEFT:
                    camera.target.x = round(std::max(camera.target.x - action.speed * GetFrameTime(), action.target));
                    if (camera.target.x == action.target) return true;
                    break;
                case DOWN:
                    camera.target.y = round(std::min(camera.target.y + action.speed * GetFrameTime(), action.target));
                    if (camera.target.y == action.target) return true;
                    break;
                case UP:
                    camera.target.y = round(std::max(camera.target.y - action.speed * GetFrameTime(), action.target));
                    if (camera.target.y == action.target) return true;
                    break;
            }
            break;
        }
        case ACTION_MOVE_NPC: {
            NPC* npc = action.npc;
            if (!action.started) {
                action.started = true;
                npc->direction = action.direction;
                switch (action.direction) {
                    case RIGHT:
                        action.target = npc->x + action.tiles * tileSize;
                        break;
                    case LEFT:
                        action.target = npc->x - action.tiles * tileSize;
                        break;
                    case DOWN:
                        action.target = npc->y + action.tiles * tileSize;
                        break;
                    case UP:
                        action.target = npc->y - action.tiles * tileSize;
                        break;
                }
            }
            
            float distance = 0.0f;
            switch (action.direction) {
                case RIGHT:
                case LEFT:
                    distance = abs(action.target - npc->x);
                    break;
                case UP:
                case DOWN:
                    distance = abs(action.target - npc->y);
                    break;
            }

            if (distance > 1.0f) {
                switch (action.direction) {
                    case RIGHT:
                        npc->x += npc->speed * GetFrameTime();
                        break;
                    case LEFT:
                        npc->x -= npc->speed * GetFrameTime();
                        break;
                    case DOWN:
                        npc->y += npc->speed * GetFrameTime();
                        break;
                    case UP:
                        npc->y -= npc->speed * GetFrameTime();
                        break;
                }
                if (action.follow)
                    camera.target = { floor(npc->x + tileSize/2.0f), floor(npc->y + tileSize/2.0f) };

                npc->updateBody();
                npc->updateFrame(GetFrameTime());

            } else {
                npc->frame = 0;
                return true;
            }
            break;
        }
        case ACTION_MOVE_PLAYER: {
            if (!action.started) {
                action.started = true;
                player.direction = action.direction;
                switch (action.direction) {
                    case RIGHT:
                        action.target = player.x + action.tiles * tileSize;
                        break;
                    case LEFT:
                        action.target = player.x - action.tiles * tileSize;
                        break;
                    case DOWN:
                        action.target = player.y + action.tiles * tileSize;
                        break;
                    case UP:
                        action.target = player.y - action.tiles * tileSize;
                        break;
                }
                player.speed = 150.0f;
                player.frameMaxTimer = 0.10f;
            }
            
            float distance = 0.0f;
            switch (action.direction) {
                case RIGHT:
                case LEFT:
                    distance = abs(action.target - player.x);
                    break;
                case UP:
                case DOWN:
                    distance = abs(action.target - player.y);
                    break;
            }

            if (distance > 1.0f) {
                switch (action.direction) {
                    case RIGHT:
                        player.x += player.speed * GetFrameTime();
                        break;
                    case LEFT:
                        player.x -= player.speed * GetFrameTime();
                        break;
                    case DOWN:
                        player.y += player.speed * GetFrameTime();
                        break;
                    case UP:
                        player.y -= player.speed * GetFrameTime();
                        break;
                }

                if (action.follow)
                    camera.target = { floor(player.x + tileSize/2.0f), floor(player.y + tileSize/2.0f) };

                player.updatePlayerBody();
                player.updatePlayerFrame(GetFrameTime());

            } else return true;
            break;
        }
        case ACTION_GROUP: {
            bool allFinished = true;
            for (EventAction& sub : action.subactions) {
                if (!sub.finished) {
                    bool done = executeAction(sub, player, camera);
                    if (!done)
                        allFinished = false;
                    else
                        sub.finished = true;
                }
            }
            if (allFinished) return true;
            break;
        }
    }
    return false;
}

int main(void)
{
    //Initialize
    initialize();

    //Texture Renderer (screen scaling)
    RenderTexture2D target = LoadRenderTexture(GAME_WIDTH, GAME_HEIGHT);

    Player player = Player();
    Camera2D camera = setupCamera(player);

    Map map = Map();
    map.loadMap("mapa_dungeon", "player_1");
    player.x = map.playerSpawn.x;
    player.y = map.playerSpawn.y;

    camera.target.x = floor(camera.target.x);
    camera.target.y = floor(camera.target.y);

    Drawable playerDraw;
    playerDraw.texture = &player.texture;

    Image textboxImage = LoadImage((RESOURCE_PATH + "textbox.png").c_str());
    Texture2D textboxTexture = LoadTextureFromImage(textboxImage);
    UnloadImage(textboxImage);

    while (!WindowShouldClose())
    {
        //Input
        input(player, map);

        // Transitions
        if (gameState == STATE_TRANSITION) {
            if (player.fading) {
                player.fadeAlpha += 1 * GetFrameTime();
                if (player.fadeAlpha >= 1.0f) {
                    player.fadeAlpha = 1.0f;

                    map.loadMap(player.pendingTransition->map, player.pendingTransition->spawnName);
                    player.x = map.playerSpawn.x;
                    player.y = map.playerSpawn.y;
                    player.updatePlayerBody();

                    player.fading = false;
                }
            } else {
                player.fadeAlpha -= 1 * GetFrameTime();
                if (player.fadeAlpha <= 0.0f) {
                    player.fadeAlpha = 0.0f;
                    gameState = STATE_NORMAL;
                    player.pendingTransition = nullptr;
                }
            }
        }

        //Camera update
        if (gameState != STATE_EVENT)
            camera.target = { floor(player.x + tileSize/2.0f), floor(player.y + tileSize/2.0f) };       //Floored to avoid visual bugs, player must also be floored
        
        // Events
        if (gameState == STATE_EVENT) {
            Event* ev = player.ongoingEvent;
            if (!ev) {
                gameState = STATE_NORMAL;
            }
            else if (ev->eventIndex >= (int) ev->actions.size()) {
                gameState = STATE_NORMAL;
                ev->eventIndex = 0;
                ev->triggered = true;
                player.ongoingEvent = nullptr;
            }
            else {
                EventAction& action = ev->actions[ev->eventIndex];
                if (executeAction(action, player, camera)) ev->eventIndex++;
            }
        }

        //Draw
        BeginTextureMode(target);

        ClearBackground(BLUE);

        BeginMode2D(camera);
        
        TileLayer above;

        // Draw map
        map.drawMap(true);

        // Draw player and other drawables
        map.dynamicDrawables.clear();

        // Player
        Rectangle src = { player.frame * player.spriteW, 
                        player.direction * player.spriteH, 
                        player.spriteW, 
                        player.spriteH };

        Rectangle dst = { floor(player.x), 
                        floor(player.y) - (player.spriteH - tileSize), 
                        player.spriteW, 
                        player.spriteH };         //Floored to avoid visual bugs, cam must also be floored

        playerDraw.src = src;
        playerDraw.dst = dst;
        playerDraw.sortY = player.body.y + player.body.height;

        map.dynamicDrawables.push_back(playerDraw);

        // NPCs
        for (NPC& npc : map.npcs) {
            Drawable d;
            d.texture = &npc.texture;

            d.src = {
                npc.frame * npc.spriteW,
                npc.direction * npc.spriteH,
                npc.spriteW,
                npc.spriteH
            };

            d.dst = {
                floor(npc.x),
                floor(npc.y) - (npc.spriteH - tileSize),
                npc.spriteW,
                npc.spriteH
            };

            d.sortY = npc.body.y + npc.body.height;

            map.dynamicDrawables.push_back(d);
        }

        // Map drawables
        std::vector<Drawable*> drawables;
        drawables.reserve(map.staticDrawables.size() + map.dynamicDrawables.size());

        for (Drawable& d : map.staticDrawables)
            drawables.push_back(&d);

        for (Drawable& d : map.dynamicDrawables)
            drawables.push_back(&d);

        std::sort(drawables.begin(), drawables.end(),
            [](Drawable* a, Drawable* b) {
                return a->sortY < b->sortY;
            }
        );

        for (Drawable* d : drawables) {
            DrawTexturePro(*d->texture, d->src, d->dst, {0,0}, 0, WHITE);
        }

        drawables.clear();

        // Draw topmost layer
        map.drawMap(false);

        // Draw debug info
        if (DEBUG_MODE) {
            DrawRectangleLinesEx(player.body, 1, GREEN);
            for (Rectangle& r : map.debugColliders) DrawRectangleLinesEx(r, 1, RED);
            map.debugColliders.clear();

            for (Transition& t : map.transitions)
                DrawRectangleLinesEx(t.trigger, 1, YELLOW);

            for (DialoguePoint& dp : map.dialoguePoints)
                DrawRectangleLinesEx(dp.trigger, 1, PURPLE);

            for (NPC& npc : map.npcs)
                DrawRectangleLinesEx(npc.body, 1, LIME);

            for (EventPoint& ep : map.eventPoints)
                DrawRectangleLinesEx(ep.trigger, 1, PINK);
        }

        // Draw fades
        if (player.fadeAlpha > 0.0f) {
            DrawRectangle(
                0, 0,
                GetScreenWidth(),
                GetScreenHeight(),
                Fade(BLACK, player.fadeAlpha)
            );
        }
        
        EndMode2D();

        // Draw dialogues
        if (gameState == STATE_DIALOGUE && player.currentDialogue) {
            const std::string& text = player.currentDialogue->msg[player.dialogueIndex];

            if (!player.lineFinished) {
                player.textTimer += GetFrameTime();
                if (player.textTimer >= player.textSpeed) {
                    player.textTimer = 0.0f;
                    player.visibleChars++;

                    if (player.visibleChars >= (int)text.size()) {
                        player.visibleChars = text.size();
                        player.lineFinished = true;
                    }
                }
            }
        }
        
        if (gameState == STATE_DIALOGUE) {
            Rectangle outer = { 300, GAME_HEIGHT - 180, GAME_WIDTH - 600, 140 };
            Rectangle inner = { 306, GAME_HEIGHT - 174, GAME_WIDTH - 612, 128 };

            // Black box (would also require updating text color)
            /*DrawRectangleRec(outer, Fade(BLACK, 0.7f));
            DrawRectangleRec(inner, Fade(DARKGRAY, 0.65f));*/

            // Texture box
            Rectangle src = {0, 0, (float)textboxTexture.width, (float)textboxTexture.height};
            DrawTexturePro(textboxTexture, src, outer, {0,0}, 0, Color {255, 255, 255, 255});

            Dialogue* d = player.currentDialogue;
            int i = player.dialogueIndex;

            std::string speaker = d->speaker[i];
            std::string fullText = d->msg[i];
            std::string visibleText = fullText.substr(0, player.visibleChars);

            DrawText(
                speaker.c_str(),
                inner.x + 10,
                inner.y + 8,
                20,
                Color {255, 255, 255, 255}
            );

            DrawText(
                visibleText.c_str(),
                inner.x + 10,
                inner.y + 46,
                20,
                Color {62, 31, 29, 255}
            );

            if (player.lineFinished) {
                DrawText(
                    "Z",
                    inner.x + inner.width - 20,
                    inner.y + inner.height - 20,
                    16,
                    Color {62, 31, 29, 255}
                );
            }
        }

        EndTextureMode();
        
        BeginDrawing();

        DrawTexturePro(target.texture, Rectangle{ 0, 0, (float)target.texture.width, -(float)target.texture.height }, 
        Rectangle{ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, Vector2{0,0}, 0, WHITE ); 

        EndDrawing();
    }

    CloseWindow();

    return 0;
}