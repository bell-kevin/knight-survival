// level.cpp - Reads the Tiled maps, and finds the way around their walls.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include "level.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string_view>
#include <utility>

namespace
{
const float SPOT_OFFSET = (2 * CELL - KNIGHT_SIZE) / 2; // a knight in the middle of its 2 x 2 cells

std::string lower(std::string text)
{
    for (char &c : text)
        c = (char)std::tolower((unsigned char)c);
    return text;
}

// "maps/level2.tmx" -> "level2"
std::string stem(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool endsWith(const std::string &text, const std::string &end)
{
    return text.size() >= end.size() && lower(text.substr(text.size() - end.size())) == end;
}

// File names in play order: "level2" comes before "level10"
bool naturalLess(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j]))
        {
            size_t endA = i, endB = j;
            while (endA < a.size() && std::isdigit((unsigned char)a[endA]))
                endA++;
            while (endB < b.size() && std::isdigit((unsigned char)b[endB]))
                endB++;
            std::string_view x(a.data() + i, endA - i), y(b.data() + j, endB - j);
            while (x.size() > 1 && x[0] == '0')
                x.remove_prefix(1);
            while (y.size() > 1 && y[0] == '0')
                y.remove_prefix(1);
            if (x.size() != y.size())
                return x.size() < y.size();
            if (x != y)
                return x < y;
            i = endA;
            j = endB;
            continue;
        }
        char x = (char)std::tolower((unsigned char)a[i]), y = (char)std::tolower((unsigned char)b[j]);
        if (x != y)
            return x < y;
        i++;
        j++;
    }
    return a.size() - i < b.size() - j;
}

// ---- Just enough XML for the files Tiled writes ---------------------------------------------

struct XmlNode
{
    std::string name;
    std::map<std::string, std::string> attributes;
    std::string text; // the text right after the start tag, like a layer's CSV numbers
    std::vector<XmlNode> children;

    std::string attr(const std::string &key, const std::string &fallback = "") const
    {
        auto it = attributes.find(key);
        return it == attributes.end() ? fallback : it->second;
    }

    double number(const std::string &key, double fallback) const
    {
        auto it = attributes.find(key);
        if (it == attributes.end())
            return fallback;
        char *end = nullptr;
        double value = std::strtod(it->second.c_str(), &end);
        return end == it->second.c_str() || !std::isfinite(value) ? fallback : value;
    }

    const XmlNode *child(const std::string &childName) const
    {
        for (auto &c : children)
        {
            if (c.name == childName)
                return &c;
        }
        return nullptr;
    }

    // Tiled's custom properties (names in lowercase): <properties><property name=".." value=".."/>
    std::map<std::string, std::string> properties() const
    {
        std::map<std::string, std::string> out;
        if (const XmlNode *list = child("properties"))
        {
            for (auto &p : list->children)
            {
                if (p.name == "property")
                    out[lower(p.attr("name"))] = p.attributes.count("value") ? p.attr("value") : p.text;
            }
        }
        return out;
    }
};

void appendUtf8(std::string &out, unsigned long code)
{
    if (code < 0x80)
    {
        out += (char)code;
    }
    else if (code < 0x800)
    {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
    else if (code < 0x110000)
    {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

// &amp; &lt; &gt; &quot; &apos; and &#123; / &#x7B;
std::string decodeEntities(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++)
    {
        size_t end = text[i] == '&' ? text.find(';', i) : std::string_view::npos;
        if (end == std::string_view::npos || end - i > 10)
        {
            out += text[i];
            continue;
        }
        std::string entity(text.substr(i + 1, end - i - 1));
        if (entity == "amp")
            out += '&';
        else if (entity == "lt")
            out += '<';
        else if (entity == "gt")
            out += '>';
        else if (entity == "quot")
            out += '"';
        else if (entity == "apos")
            out += '\'';
        else if (entity.size() > 1 && entity[0] == '#')
            appendUtf8(out, entity[1] == 'x' || entity[1] == 'X' ? std::strtoul(entity.c_str() + 2, nullptr, 16)
                                                                   : std::strtoul(entity.c_str() + 1, nullptr, 10));
        else
            out.append(text.substr(i, end - i + 1)); // not one we know: leave it alone
        i = end;
    }
    return out;
}

struct Tag
{
    std::string name; // "/name" for an end tag
    std::map<std::string, std::string> attributes;
    bool selfClosing = false; // <name ... />
    std::string text;         // the text after it, up to the next tag
};

// Splits a document into its tags (skipping <?xml?>, comments and the like)
bool readTags(const std::string &xml, std::vector<Tag> &tags, std::string &problem)
{
    size_t i = xml.find('<');
    while (i != std::string::npos)
    {
        size_t end = std::string::npos;
        std::string text;
        bool isTag = false;
        if (xml.compare(i, 4, "<!--") == 0)
        {
            end = xml.find("-->", i + 4);
            end = end == std::string::npos ? end : end + 2;
        }
        else if (xml.compare(i, 9, "<![CDATA[") == 0)
        {
            end = xml.find("]]>", i + 9);
            if (end != std::string::npos)
                text = xml.substr(i + 9, end - i - 9);
            end = end == std::string::npos ? end : end + 2;
        }
        else if (xml.compare(i, 2, "<?") == 0)
        {
            end = xml.find("?>", i + 2);
            end = end == std::string::npos ? end : end + 1;
        }
        else
        {
            isTag = xml.compare(i, 2, "<!") != 0; // <!DOCTYPE ...> is skipped like a comment
            char quote = 0;
            for (end = i + 1; end < xml.size() && (quote || xml[end] != '>'); end++)
            {
                if (quote && xml[end] == quote)
                    quote = 0;
                else if (!quote && (xml[end] == '"' || xml[end] == '\''))
                    quote = xml[end];
            }
            if (end >= xml.size())
                end = std::string::npos;
        }
        if (end == std::string::npos)
        {
            problem = "the XML ends in the middle of a tag";
            return false;
        }

        if (isTag)
        {
            std::string_view body(xml.data() + i + 1, end - i - 1);
            Tag tag;
            if (!body.empty() && body.back() == '/')
            {
                tag.selfClosing = true;
                body.remove_suffix(1);
            }
            size_t p = 0;
            while (p < body.size() && !std::isspace((unsigned char)body[p]))
                p++;
            tag.name = std::string(body.substr(0, p));
            if (tag.name.empty() || tag.name == "/")
            {
                problem = "a tag without a name";
                return false;
            }
            while (true)
            {
                while (p < body.size() && std::isspace((unsigned char)body[p]))
                    p++;
                if (p >= body.size())
                    break;
                size_t equals = body.find('=', p);
                if (equals == std::string_view::npos)
                {
                    problem = "a broken attribute in <" + tag.name + ">";
                    return false;
                }
                std::string_view key = body.substr(p, equals - p);
                while (!key.empty() && std::isspace((unsigned char)key.back()))
                    key.remove_suffix(1);
                size_t open = equals + 1;
                while (open < body.size() && std::isspace((unsigned char)body[open]))
                    open++;
                size_t close = open < body.size() ? body.find(body[open], open + 1) : std::string_view::npos;
                if (open >= body.size() || (body[open] != '"' && body[open] != '\'') || close == std::string_view::npos)
                {
                    problem = "an attribute without quotes in <" + tag.name + ">";
                    return false;
                }
                tag.attributes[std::string(key)] = decodeEntities(body.substr(open + 1, close - open - 1));
                p = close + 1;
            }
            tags.push_back(std::move(tag));
        }
        else if (!text.empty() && !tags.empty())
        {
            tags.back().text += text; // CDATA belongs to the element it's in
        }

        size_t next = xml.find('<', end + 1);
        if (!tags.empty())
            tags.back().text += decodeEntities(std::string_view(xml).substr(end + 1, (next == std::string::npos ? xml.size() : next) - end - 1));
        i = next;
    }
    return true;
}

// Builds the element that starts at tags[i] (and everything in it); i ends up after it
bool buildTree(const std::vector<Tag> &tags, size_t &i, XmlNode &node, int depth, std::string &problem)
{
    const Tag &start = tags[i++];
    node.name = start.name;
    node.attributes = start.attributes;
    node.text = start.text;
    if (start.selfClosing)
        return true;
    while (i < tags.size())
    {
        if (tags[i].name[0] == '/')
        {
            if (tags[i].name.compare(1, std::string::npos, node.name) != 0)
            {
                problem = "<" + node.name + "> is closed by <" + tags[i].name + ">";
                return false;
            }
            i++;
            return true;
        }
        node.children.emplace_back();
        if (depth > 40 || !buildTree(tags, i, node.children.back(), depth + 1, problem))
        {
            if (problem.empty())
                problem = "the XML is nested too deep";
            return false;
        }
    }
    problem = "<" + node.name + "> is never closed";
    return false;
}

bool parseXml(const std::string &xml, XmlNode &root, std::string &problem)
{
    std::vector<Tag> tags;
    if (!readTags(xml, tags, problem))
        return false;
    if (tags.empty() || tags[0].name[0] == '/')
    {
        problem = "there is no XML in it";
        return false;
    }
    size_t i = 0;
    return buildTree(tags, i, root, 0, problem);
}

// ---- Tiled maps -----------------------------------------------------------------------------

const MapFile *findFile(const std::vector<MapFile> &files, const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (auto &file : files)
    {
        if (file.name == name)
            return &file;
    }
    return nullptr;
}

// "../assets/Tiles/MossyStone.bmp" -> "Tiles/MossyStone.bmp". The game looks for pictures in its
// assets folder, and it can only read .bmp pictures.
bool picturePath(const std::string &source, std::string &file, std::string &problem)
{
    std::string path = source;
    std::replace(path.begin(), path.end(), '\\', '/');
    size_t at = path.rfind("assets/");
    if (at == std::string::npos || (at > 0 && path[at - 1] != '/'))
    {
        problem = "the tile picture " + source + " is not in the assets folder";
        return false;
    }
    file = path.substr(at + 7);
    if (!endsWith(file, ".bmp"))
    {
        problem = "the tile picture " + source + " is not a .bmp file (the game can only read BMP pictures)";
        return false;
    }
    return true;
}

bool readTileset(const XmlNode &tileset, unsigned firstNumber, std::vector<TilePicture> &pictures, std::string &problem)
{
    const unsigned MAX_TILES = 65536;
    int tileW = (int)tileset.number("tilewidth", 0), tileH = (int)tileset.number("tileheight", 0);
    int columns = (int)tileset.number("columns", 0), count = (int)tileset.number("tilecount", 0);
    int spacing = (int)tileset.number("spacing", 0), margin = (int)tileset.number("margin", 0);
    auto put = [&](unsigned id, const TilePicture &picture) {
        unsigned number = firstNumber + id;
        if (id >= MAX_TILES || number >= MAX_TILES)
            return;
        if (pictures.size() <= number)
            pictures.resize(number + 1);
        pictures[number] = picture;
    };

    for (auto &child : tileset.children)
    {
        if (child.name == "image") // one picture with all the tiles in rows
        {
            std::string file;
            if (!picturePath(child.attr("source"), file, problem))
                return false;
            if (tileW <= 0 || tileH <= 0)
            {
                problem = "the tileset has no tile size";
                return false;
            }
            int imageW = (int)child.number("width", 0), imageH = (int)child.number("height", 0);
            if (columns <= 0)
                columns = (imageW - 2 * margin + spacing) / (tileW + spacing);
            if (count <= 0 && columns > 0)
                count = columns * ((imageH - 2 * margin + spacing) / (tileH + spacing));
            for (int t = 0; columns > 0 && t < count && t < (int)MAX_TILES; t++)
                put((unsigned)t, {file, margin + (t % columns) * (tileW + spacing), margin + (t / columns) * (tileH + spacing), tileW, tileH});
        }
        else if (child.name == "tile") // a collection of pictures, one per tile
        {
            const XmlNode *image = child.child("image");
            if (!image)
                continue;
            TilePicture picture;
            if (!picturePath(image->attr("source"), picture.file, problem))
                return false;
            if (child.attributes.count("width")) // just part of the picture
            {
                picture.x = (int)child.number("x", 0);
                picture.y = (int)child.number("y", 0);
                picture.w = (int)child.number("width", 0);
                picture.h = (int)child.number("height", 0);
            }
            double id = child.number("id", -1);
            if (id >= 0)
                put((unsigned)id, picture);
        }
    }
    return true;
}

bool readLayerData(const XmlNode &layer, int cells, std::vector<unsigned> &out, std::string &problem)
{
    std::string name = layer.attr("name");
    const XmlNode *data = layer.child("data");
    if (!data)
    {
        problem = "the layer \"" + name + "\" has no tiles";
        return false;
    }
    std::string encoding = data->attr("encoding");
    out.clear();
    if (encoding == "csv")
    {
        const char *p = data->text.c_str();
        while (*p)
        {
            if (std::isdigit((unsigned char)*p))
            {
                char *end = nullptr;
                out.push_back((unsigned)std::strtoull(p, &end, 10));
                p = end;
            }
            else
            {
                p++; // commas and line breaks
            }
        }
    }
    else if (encoding.empty()) // the old XML layer format: <tile gid="..."/> for every cell
    {
        for (auto &tile : data->children)
        {
            if (tile.name == "tile")
                out.push_back((unsigned)std::strtoull(tile.attr("gid", "0").c_str(), nullptr, 10));
        }
    }
    else
    {
        problem = "the layer \"" + name + "\" is saved as " + encoding +
                  ": in Tiled, set Map > Map Properties > Tile Layer Format to CSV";
        return false;
    }
    if ((int)out.size() != cells)
    {
        problem = "the layer \"" + name + "\" has " + std::to_string(out.size()) + " tiles instead of " + std::to_string(cells);
        return false;
    }
    return true;
}

// "red, scout, champion, brute and warlord"
std::string enemyKindList()
{
    std::string list;
    for (int kind = 0; kind < ENEMY_KINDS; kind++)
        list += std::string(kind == 0 ? "" : kind + 1 == ENEMY_KINDS ? " and " : ", ") + ENEMY_TYPES[kind].name;
    return list;
}

// Which kind of bad guy an object is: its class (Tiled saves that as "type", except Tiled 1.9,
// which saved it as "class"), or else a "kind" property, or else its name. A red knight unless
// it says otherwise.
int enemyKindOf(const XmlNode &object, Level &level)
{
    auto properties = object.properties();
    for (const std::string &word : {object.attr("type"), object.attr("class"), properties["kind"]})
    {
        if (word.empty())
            continue;
        int kind = enemyKindNamed(word);
        if (kind >= 0)
            return kind;
        level.warnings.push_back("there is no bad guy called \"" + word + "\" (only " + enemyKindList() + "), so it's a red knight");
        return RED_KNIGHT;
    }
    int kind = enemyKindNamed(object.attr("name")); // names can be anything: no warning for "guard 1"
    return kind >= 0 ? kind : RED_KNIGHT;
}

void readObjects(const XmlNode &group, float scale, Level &level)
{
    std::string kind = lower(group.attr("name"));
    for (auto &object : group.children)
    {
        if (object.name != "object")
            continue;
        float x = (float)object.number("x", 0) * scale, y = (float)object.number("y", 0) * scale;
        float w = (float)object.number("width", 0) * scale, h = (float)object.number("height", 0) * scale;
        if (object.attributes.count("gid"))
            y -= h; // a tile object hangs from its bottom-left corner
        Point middle{x + w / 2, y + h / 2};
        std::string name = lower(object.attr("name"));

        if (kind == "doors")
        {
            Rect area = w >= 1 && h >= 1 ? Rect{x, y, w, h} : Rect{middle.x - CELL / 2.0f, middle.y - CELL / 2.0f, (float)CELL, (float)CELL};
            level.doors.push_back({area, object.properties()["destination"], 0});
        }
        else if (kind == "spawn points" || kind == "spawns")
        {
            if (name == "player" || name.empty())
                level.playerSpawns.push_back(middle);
            else if (name == "enemy")
                level.enemySpawns.push_back({middle.x, middle.y, enemyKindOf(object, level)});
        }
        else if (kind == "enemies")
        {
            level.enemyStarts.push_back({middle.x, middle.y, enemyKindOf(object, level)});
        }
    }
}

// The layers of a map (or of a group layer in it), bottom first
bool readLayers(const XmlNode &parent, const std::vector<MapFile> &files, float scale, Level &level, std::string &problem)
{
    for (auto &child : parent.children)
    {
        if (child.name == "tileset")
        {
            unsigned first = (unsigned)child.number("firstgid", 1);
            if (child.attributes.count("source")) // a tileset of its own (.tsx), like maps/tiles.tsx
            {
                const MapFile *file = findFile(files, child.attr("source"));
                XmlNode tileset;
                if (!file)
                {
                    problem = "the tileset " + child.attr("source") + " is missing (it must be in the maps folder)";
                    return false;
                }
                if (!parseXml(file->text, tileset, problem) || tileset.name != "tileset")
                {
                    problem = file->name + ": " + (problem.empty() ? "not a Tiled tileset" : problem);
                    return false;
                }
                if (!readTileset(tileset, first, level.pictures, problem))
                    return false;
            }
            else if (!readTileset(child, first, level.pictures, problem))
            {
                return false;
            }
        }
        else if (child.name == "layer")
        {
            std::vector<unsigned> cells;
            if (!readLayerData(child, level.cols * level.rows, cells, problem))
                return false;
            if (lower(child.attr("name")) == "walls") // everything on the "walls" layer is solid
            {
                for (size_t i = 0; i < cells.size(); i++)
                {
                    if (cells[i] & TILE_NUMBER)
                        level.walls[i] = 1;
                }
            }
            if (child.attr("visible", "1") != "0")
                level.layers.push_back(std::move(cells));
        }
        else if (child.name == "objectgroup")
        {
            readObjects(child, scale, level);
        }
        else if (child.name == "group" && !readLayers(child, files, scale, level, problem))
        {
            return false;
        }
    }
    return true;
}

bool readMap(const std::string &fileName, const XmlNode &map, const std::vector<MapFile> &files, Level &level, std::string &problem)
{
    if (map.name != "map")
    {
        problem = "not a Tiled map";
        return false;
    }
    if (map.attr("orientation", "orthogonal") != "orthogonal")
    {
        problem = "the map is " + map.attr("orientation") + ": only orthogonal maps work";
        return false;
    }
    if (map.attr("infinite") == "1")
    {
        problem = "the map is infinite: untick Infinite in Map > Map Properties";
        return false;
    }
    const int maxCols = W / CELL, maxRows = (H + CELL - 1) / CELL;
    int cols = (int)map.number("width", 0), rows = (int)map.number("height", 0);
    int tileW = (int)map.number("tilewidth", 0), tileH = (int)map.number("tileheight", 0);
    if (cols < 2 || rows < 2 || cols > maxCols || rows > maxRows)
    {
        problem = "the map is " + std::to_string(cols) + " x " + std::to_string(rows) + " tiles, but the field only has room for " +
                  std::to_string(maxCols) + " x " + std::to_string(maxRows);
        return false;
    }
    if (tileW <= 0 || tileW != tileH)
    {
        problem = "the tiles must be square";
        return false;
    }

    level = Level{};
    level.id = stem(fileName);
    level.cols = cols;
    level.rows = rows;
    level.walls.assign((size_t)(cols * rows), 0);
    auto properties = map.properties();
    level.name = properties["name"].empty() ? level.id : properties["name"];
    if (!properties["coins"].empty())
        level.coins = std::clamp(std::atoi(properties["coins"].c_str()), 0, 100000);

    if (!readLayers(map, files, (float)CELL / (float)tileW, level, problem))
        return false;
    level.prepare();
    return true;
}
} // namespace

// ---- Bad guys -------------------------------------------------------------------------------

int enemyKindNamed(const std::string &word)
{
    std::string name; // just the letters, in lowercase: "Red Knight" -> "redknight"
    for (char c : word)
    {
        if (std::isalpha((unsigned char)c))
            name += (char)std::tolower((unsigned char)c);
    }
    if (name == "knight" || name == "redknight")
        return RED_KNIGHT;
    if (name == "boss")
        return WARLORD;
    for (int kind = 0; kind < ENEMY_KINDS; kind++)
    {
        if (name == ENEMY_TYPES[kind].name)
            return kind;
    }
    return -1;
}

// ---- Walls ----------------------------------------------------------------------------------

bool Level::wall(int col, int row) const
{
    if (col < 0 || row < 0 || col >= cols || row >= rows)
        return true;
    return walls[(size_t)(row * cols + col)] != 0;
}

bool Level::canStand(int col, int row) const
{
    if (col < 0 || row < 0 || col >= cols || row >= rows)
        return false;
    return standable[(size_t)(row * cols + col)] != 0;
}

bool Level::blocked(const Rect &r) const
{
    if (r.x < 0 || r.y < 0 || r.x + r.w > W || r.y + r.h > H)
        return true;
    int col0 = (int)std::floor(r.x / CELL), col1 = (int)std::ceil((r.x + r.w) / CELL) - 1;
    int row0 = (int)std::floor(r.y / CELL), row1 = (int)std::ceil((r.y + r.h) / CELL) - 1;
    for (int row = row0; row <= row1; row++)
    {
        for (int col = col0; col <= col1; col++)
        {
            if (wall(col, row))
                return true;
        }
    }
    return false;
}

namespace
{
// Moves r along one axis; if that runs into a wall, it goes right up to the wall instead
void moveAlong(const Level &level, Rect &r, float distance, bool horizontal)
{
    if (distance == 0)
        return;
    Rect next = r;
    float &position = horizontal ? next.x : next.y;
    float size = horizontal ? next.w : next.h;
    position = std::clamp(position + distance, 0.0f, (horizontal ? (float)W : (float)H) - size);
    if (level.blocked(next))
    {
        position = distance > 0 ? std::floor((position + size) / CELL) * CELL - size : std::floor(position / CELL) * CELL + CELL;
        if (level.blocked(next))
            return;
    }
    r = next;
}
} // namespace

void Level::slide(Rect &r, float dx, float dy) const
{
    if (blocked(r)) // stuck in a wall already (in a world we just joined, say): walk out freely
    {
        r.x = std::clamp(r.x + dx, 0.0f, W - r.w);
        r.y = std::clamp(r.y + dy, 0.0f, H - r.h);
        return;
    }
    // In steps of a quarter cell, so nothing can jump through a wall
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(dx), std::fabs(dy)) / (CELL / 4.0f)));
    for (int i = 0; i < steps; i++)
    {
        moveAlong(*this, r, dx / (float)steps, true);
        moveAlong(*this, r, dy / (float)steps, false);
    }
}

bool Level::clearPath(const Rect &from, Point to) const
{
    float dx = to.x - from.x, dy = to.y - from.y;
    int steps = (int)std::ceil(std::hypot(dx, dy) / (CELL / 2.0f));
    for (int i = 1; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        if (blocked({from.x + dx * t, from.y + dy * t, from.w, from.h}))
            return false;
    }
    return true;
}

// ---- Finding the way ------------------------------------------------------------------------

Point Level::spotPosition(int spot) const
{
    return {(float)(spot % cols * CELL) + SPOT_OFFSET, (float)(spot / cols * CELL) + SPOT_OFFSET};
}

int Level::spotNear(float x, float y) const
{
    int col = (int)std::lround((x - SPOT_OFFSET) / CELL), row = (int)std::lround((y - SPOT_OFFSET) / CELL);
    int best = -1;
    float bestDistance = 0;
    for (int r = row - 1; r <= row + 1; r++)
    {
        for (int c = col - 1; c <= col + 1; c++)
        {
            if (!canStand(c, r))
                continue;
            float d = std::hypot((float)(c * CELL) + SPOT_OFFSET - x, (float)(r * CELL) + SPOT_OFFSET - y);
            if (best < 0 || d < bestDistance)
            {
                best = r * cols + c;
                bestDistance = d;
            }
        }
    }
    return best;
}

std::vector<int> Level::distances(const std::vector<int> &from) const
{
    std::vector<int> distance((size_t)(cols * rows), -1);
    std::vector<int> queue;
    for (int spot : from)
    {
        if (spot >= 0 && spot < cols * rows && distance[(size_t)spot] < 0)
        {
            distance[(size_t)spot] = 0;
            queue.push_back(spot);
        }
    }
    const int dc[4] = {1, -1, 0, 0}, dr[4] = {0, 0, 1, -1};
    for (size_t i = 0; i < queue.size(); i++)
    {
        int spot = queue[i], col = spot % cols, row = spot / cols;
        for (int k = 0; k < 4; k++)
        {
            int next = (row + dr[k]) * cols + col + dc[k];
            if (canStand(col + dc[k], row + dr[k]) && distance[(size_t)next] < 0)
            {
                distance[(size_t)next] = distance[(size_t)spot] + 1;
                queue.push_back(next);
            }
        }
    }
    return distance;
}

int Level::downhill(int spot, const std::vector<int> &distance) const
{
    int col = spot % cols, row = spot / cols, best = spot;
    for (int dr = -1; dr <= 1; dr++)
    {
        for (int dc = -1; dc <= 1; dc++)
        {
            if ((dc == 0 && dr == 0) || !canStand(col + dc, row + dr))
                continue;
            if (dc != 0 && dr != 0 && (!canStand(col + dc, row) || !canStand(col, row + dr)))
                continue; // diagonally only where both sides are free: no cutting corners
            int next = (row + dr) * cols + col + dc;
            if (distance[(size_t)next] >= 0 && (distance[(size_t)best] < 0 || distance[(size_t)next] < distance[(size_t)best]))
                best = next;
        }
    }
    return best;
}

void Level::prepare()
{
    standable.assign((size_t)(cols * rows), 0);
    for (int row = 0; row + 1 < rows; row++)
    {
        for (int col = 0; col + 1 < cols; col++)
        {
            bool free = !wall(col, row) && !wall(col + 1, row) && !wall(col, row + 1) && !wall(col + 1, row + 1);
            bool onField = (float)(col * CELL) + SPOT_OFFSET + KNIGHT_SIZE <= W && (float)(row * CELL) + SPOT_OFFSET + KNIGHT_SIZE <= H;
            standable[(size_t)(row * cols + col)] = free && onField;
        }
    }

    // Everywhere the players can walk to from where they start
    std::vector<int> starts;
    for (Point p : playerSpawns)
    {
        int spot = spotNear(p.x - KNIGHT_SIZE / 2, p.y - KNIGHT_SIZE / 2);
        if (spot >= 0)
            starts.push_back(spot);
    }
    if (starts.empty())
    {
        for (int spot = 0; spot < cols * rows; spot++)
        {
            if (standable[(size_t)spot])
                starts.push_back(spot);
        }
    }
    std::vector<int> distance = distances(starts);
    std::vector<unsigned char> reachable((size_t)(cols * rows), 0);
    open.clear();
    for (int spot = 0; spot < cols * rows; spot++)
    {
        if (distance[(size_t)spot] < 0)
            continue;
        open.push_back(spot);
        for (int cell : {spot, spot + 1, spot + cols, spot + cols + 1})
            reachable[(size_t)cell] = 1;
    }

    // Coins go on those cells, but not on a door or right next to one: picking up a coin should
    // never take the team through an open door by accident (and the EXIT sign stays clear)
    coinCells.clear();
    for (int cell = 0; cell < cols * rows; cell++)
    {
        Rect box{(float)(cell % cols * CELL), (float)(cell / cols * CELL), (float)CELL, (float)CELL};
        bool byDoor = std::any_of(doors.begin(), doors.end(), [&](const Door &d) {
            const float m = CELL / 2.0f;
            return checkCollision({d.area.x - m, d.area.y - m, d.area.w + 2 * m, d.area.h + 2 * m}, box);
        });
        if (reachable[(size_t)cell] && !byDoor)
            coinCells.push_back(cell);
    }
}

// ---- Loading --------------------------------------------------------------------------------

bool loadLevel(const std::string &fileName, const std::vector<MapFile> &files, Level &out, std::string &problem)
{
    const MapFile *file = findFile(files, fileName);
    XmlNode map;
    if (!file)
    {
        problem = "there is no such map";
        return false;
    }
    problem.clear();
    return parseXml(file->text, map, problem) && readMap(file->name, map, files, out, problem);
}

LevelSet loadLevels(const std::vector<MapFile> &files)
{
    LevelSet set;
    std::vector<const MapFile *> maps;
    for (auto &file : files)
    {
        if (endsWith(file.name, ".tmx"))
            maps.push_back(&file);
    }
    std::sort(maps.begin(), maps.end(), [](const MapFile *a, const MapFile *b) { return naturalLess(a->name, b->name); });
    for (const MapFile *file : maps)
    {
        Level level;
        std::string problem;
        if (loadLevel(file->name, files, level, problem))
        {
            for (const std::string &warning : level.warnings)
                set.problems.push_back(file->name + ": " + warning);
            set.levels.push_back(std::move(level));
        }
        else
        {
            set.problems.push_back(file->name + ": " + problem);
        }
    }
    if (set.levels.empty())
        set.levels.push_back(openField());

    // Where each door leads: the level it names, or else the next one (after the last: the first)
    for (size_t i = 0; i < set.levels.size(); i++)
    {
        for (Door &door : set.levels[i].doors)
        {
            door.destination = (int)((i + 1) % set.levels.size());
            if (door.destinationId.empty())
                continue;
            std::string wanted = lower(stem(door.destinationId));
            auto found = std::find_if(set.levels.begin(), set.levels.end(), [&](const Level &l) { return lower(l.id) == wanted; });
            if (found != set.levels.end())
                door.destination = (int)(found - set.levels.begin());
            else
                set.problems.push_back(set.levels[i].id + ".tmx: a door leads to \"" + door.destinationId + "\", but there is no such map");
        }
    }
    return set;
}

const LevelSet &builtInLevels()
{
    static const LevelSet set = [] {
        LevelSet levels = loadLevels(builtInMapFiles());
        for (auto &problem : levels.problems)
            std::fprintf(stderr, "Level problem: %s\n", problem.c_str());
        return levels;
    }();
    return set;
}

const Level &builtInLevel(int index)
{
    const auto &levels = builtInLevels().levels;
    return levels[index >= 0 && index < (int)levels.size() ? (size_t)index : 0];
}

Level openField()
{
    Level level;
    level.id = "open-field";
    level.name = "The Open Field";
    level.cols = W / CELL;
    level.rows = (H + CELL - 1) / CELL;
    level.walls.assign((size_t)(level.cols * level.rows), 0);
    level.prepare();
    std::erase_if(level.coinCells, [&](int cell) { return cell / level.cols < 2; }); // not under the score
    return level;
}
