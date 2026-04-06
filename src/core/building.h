#pragma once

#include <fmt/base.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <optional>
#include <pugixml.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/cell_coord.h"
#include "exceptions.h"
#include "files/lotheader.h"
#include "files/lotpack.h"
#include "files/prefab.h"
#include "files/tiledefinition.h"
#include "io/file.h"
#include "logger.h"
#include "math/rectangle.h"
#include "math/vector2i.h"
#include "services/tiles_manager.h"
#include "types.h"

using TileData = TileDefinition::TileData;

struct BuildingFloor;

inline std::string formatTileName(std::string_view tilename)
{
    size_t lastUnderscore = tilename.find_last_of('_');

    if (lastUnderscore == std::string_view::npos || lastUnderscore == tilename.size() - 1)
    {
        return std::string(tilename);
    }

    std::string_view prefix = tilename.substr(0, lastUnderscore + 1);
    std::string_view suffix = tilename.substr(lastUnderscore + 1);

    size_t firstNonZero = suffix.find_first_not_of('0');

    std::string result;
    result.reserve(tilename.size());
    result.append(prefix);

    if (firstNonZero == std::string_view::npos)
    {
        result.push_back('0');
    }
    else
    {
        result.append(suffix.substr(firstNonZero));
    }

    return result;
}

inline std::unordered_set<std::string> split(const std::string &input, char delimiter = ',')
{
    std::unordered_set<std::string> result;

    if (input.empty())
    {
        return result;
    }

    size_t start = 0;
    size_t end = input.find(delimiter);

    while (end != std::string_view::npos)
    {
        result.emplace(input.substr(start, end - start));
        start = end + 1;
        end = input.find(delimiter, start);
    }

    result.emplace(input.substr(start));

    return result;
}

inline std::string join(const std::unordered_set<std::string> &input_set, std::string_view delimiter = ",")
{
    if (input_set.empty())
    {
        return "";
    }

    std::vector<std::string> sortedValues(input_set.begin(), input_set.end());

    std::sort(sortedValues.begin(), sortedValues.end(), [](const std::string &a, const std::string &b)
    {
        return a < b;
    });

    std::string result;

    result.reserve(sortedValues.size() * (10 + delimiter.size()));

    auto it = sortedValues.begin();

    // On ajoute le premier élément tout seul
    result += *it;
    ++it;

    for (; it != sortedValues.end(); ++it)
    {
        result += delimiter;
        result += *it;
    }

    return result;
}

struct Building : public Loggable<Building>
{
    struct DoorObject;
    struct StairsObject;
    struct WindowObject;
    struct FurnitureObject;
    struct RoofObject;
    struct WallObject;

    enum Direction : int8_t
    {
        Invalid = -1,
        N,
        S,
        E,
        W,
        SW,
        NW,
        NE,
        SE,
    };

    struct BuildingTile
    {
        Vector2i tileOffset = { 0, 0 };
        const TileDefinition::TileData *tileData;
    };

    struct BuildingTileEntry
    {
        std::string category;
        std::unordered_map<std::string, BuildingTile> tiles;
    };

    struct FurnitureTile
    {
        struct Tile
        {
            int x = 0;
            int y = 0;
            const TileDefinition::TileData *tile;

            Tile(int x, int y, const TileDefinition::TileData *tile) : x(x), y(y), tile(tile) {}
        };

        std::vector<Tile> tiles;

        int32_t xmin = INT32_MAX;
        int32_t ymin = INT32_MAX;
        int32_t xmax = INT32_MIN;
        int32_t ymax = INT32_MIN;

        bool grime = true;

        int32_t width() const { return xmax - xmin; }
        int32_t height() const { return ymax - ymin; }

        const Tile *tileAt(int x, int y) const
        {
            for (const auto &tile : tiles)
            {
                if (tile.x == x && tile.y == y)
                {
                    return &tile;
                }
            }

            return nullptr;
        }

        void addTile(int x, int y, const TileDefinition::TileData *tile)
        {
            xmin = std::min(xmin, x);
            xmax = std::max(xmax, y);
            ymin = std::min(ymin, y);
            ymax = std::max(xmax, x);

            tiles.emplace_back(x, y, tile);
        }
    };

    struct FurnitureTiles : public Loggable<FurnitureTiles>
    {
        enum FurnitureLayer
        {
            InvalidLayer = -1,
            LayerWalls,
            LayerRoofCap,
            LayerWallOverlay,
            LayerWallFurniture,
            LayerFurniture,
            LayerFrames,
            LayerDoors,
            LayerRoof,
            LayerFloorFurniture,
            LayerCount
        };

        FurnitureLayer layer = LayerFurniture;
        Direction orient = Invalid;
        bool corners = false;

        std::unordered_map<Direction, FurnitureTile> tiles;

        void setLayer(const pugi::xml_node &furnitureNode)
        {
            auto layerAttr = furnitureNode.attribute("layer");

            if (layerAttr.empty())
                return;

            static const std::unordered_map<std::string_view, FurnitureLayer> layerLookup = {
                { "Walls", LayerWalls },
                { "RoofCap", LayerRoofCap },
                { "WallOverlay", LayerWallOverlay },
                { "WallFurniture", LayerWallFurniture },
                { "Furniture", LayerFurniture },
                { "Frames", LayerFrames },
                { "Doors", LayerDoors },
                { "Roof", LayerRoof },
                { "FloorFurniture", LayerFloorFurniture },
            };

            const std::string strLayer = layerAttr.as_string();

            auto it = layerLookup.find(strLayer);
            if (it != layerLookup.end())
            {
                layer = it->second;
            }
            else
            {
                layer = InvalidLayer;
            }
        }

        const FurnitureTile *tryGet(Direction direction) const
        {
            auto it = tiles.find(direction);
            if (it != tiles.end())
            {
                return &it->second;
            }

            if (tiles.size() == 1)
            {
                return &tiles.begin()->second;
            }

            Direction fixed = switchDirection(direction);
            it = tiles.find(fixed);
            if (it != tiles.end())
            {
                return &it->second;
            }

            logger.warning("direction not found: '{}'", magic_enum::enum_name(direction));

            return nullptr;
        }
    };

    struct BuildingRoom
    {
        std::string name;
        std::string internalName;

        int32_t layer = -1;

        uint32_t interiorWall = 0;
        uint32_t interiorWallTrim = 0;
        uint32_t floor = 0;
        uint32_t grimeFloor = 0;
        uint32_t grimeWall = 0;

        bool emptyOutside() const
        {
            return name == "emptyoutside";
        }
    };

    struct BuildingObject
    {
        Direction direction = Invalid;
        int32_t x = 0;
        int32_t y = 0;

        virtual ~BuildingObject() = default;

        virtual DoorObject *asDoor() { return nullptr; }
        virtual StairsObject *asStairs() { return nullptr; }
        virtual WindowObject *asWindow() { return nullptr; }
        virtual FurnitureObject *asFurniture() { return nullptr; }
        virtual RoofObject *asRoof() { return nullptr; }
        virtual WallObject *asWall() { return nullptr; }

        bool isNorth() const { return direction == N; }
        bool isSouth() const { return direction == S; }
        bool isEast() const { return direction == E; }
        bool isWest() const { return direction == W; }
    };

    struct DoorObject : public BuildingObject
    {
        int32_t tile = 0;
        int32_t frameTile = 0;

        DoorObject *asDoor() override { return this; }
    };

    struct StairsObject : public BuildingObject
    {
        int32_t tile = 0;

        StairsObject *asStairs() override { return this; }
    };

    struct WindowObject : public BuildingObject
    {
        int32_t tile = 0;
        int32_t curtainsTile = 0;
        int32_t shuttersTile = 0;

        WindowObject *asWindow() override { return this; }
    };

    struct FurnitureObject : public BuildingObject
    {
        int32_t furnitureTiles = 0;

        FurnitureObject *asFurniture() override { return this; }
    };

    struct RoofObject : public BuildingObject
    {
        // clang-format off
        enum RoofTile {
            SlopeS1, SlopeS2, SlopeS3,
            SlopeE1, SlopeE2, SlopeE3,
            SlopePt5S, SlopePt5E,
            SlopeOnePt5S, SlopeOnePt5E,
            SlopeTwoPt5S, SlopeTwoPt5E,

            // Shallow sides
            ShallowSlopeW1, ShallowSlopeW2,
            ShallowSlopeE1, ShallowSlopeE2,
            ShallowSlopeN1, ShallowSlopeN2,
            ShallowSlopeS1, ShallowSlopeS2,

            // Corners
            Inner1, Inner2, Inner3,
            Outer1, Outer2, Outer3,
            InnerPt5, InnerOnePt5, InnerTwoPt5,
            OuterPt5, OuterOnePt5, OuterTwoPt5,
            CornerSW1, CornerSW2, CornerSW3,
            CornerNE1, CornerNE2, CornerNE3,

            // Caps
            CapRiseE1, CapRiseE2, CapRiseE3, CapFallE1, CapFallE2, CapFallE3,
            CapRiseS1, CapRiseS2, CapRiseS3, CapFallS1, CapFallS2, CapFallS3,
            PeakPt5S, PeakPt5E,
            PeakOnePt5S, PeakOnePt5E,
            PeakTwoPt5S, PeakTwoPt5E,
            CapGapS1, CapGapS2, CapGapS3,
            CapGapE1, CapGapE2, CapGapE3,

            // Cap tiles for shallow (garage, trailer, etc) roofs
            CapShallowRiseS1, CapShallowRiseS2, CapShallowFallS1, CapShallowFallS2,
            CapShallowRiseE1, CapShallowRiseE2, CapShallowFallE1, CapShallowFallE2,

            TileCount
        };
        // clang-format on

        enum RoofType
        {
            SlopeW,
            SlopeN,
            SlopeE,
            SlopeS,
            PeakWE,
            PeakNS,
            DormerW,
            DormerN,
            DormerE,
            DormerS,
            FlatTop,

            ShallowSlopeW,
            ShallowSlopeN,
            ShallowSlopeE,
            ShallowSlopeS,
            ShallowPeakWE,
            ShallowPeakNS,

            CornerInnerSW,
            CornerInnerNW,
            CornerInnerNE,
            CornerInnerSE,
            CornerOuterSW,
            CornerOuterNW,
            CornerOuterNE,
            CornerOuterSE,
            InvalidType
        };

        enum Depth
        {
            Zero,
            Point5,
            One,
            OneP5,
            Two,
            TwoP5,
            Three,
            InvalidDepth
        };

        int32_t width = -1;
        int32_t height = -1;

        int32_t slopeTiles = 0;
        int32_t capTiles = 0;
        int32_t topTiles = 0;
        int32_t tile = 0;

        RoofType roofType = InvalidType;
        Depth roofDepth = InvalidDepth;

        bool halfDepth = false;
        bool isCappedW = false;
        bool isCappedN = false;
        bool isCappedE = false;
        bool isCappedS = false;

        RoofObject(const pugi::xml_node &objectNode)
        {
            setRoofType(objectNode);
            setRoofDepth(objectNode);

            x = objectNode.attribute("x").as_int();
            y = objectNode.attribute("y").as_int();

            capTiles = objectNode.attribute("CapTiles").as_int();
            slopeTiles = objectNode.attribute("SlopeTiles").as_int();
            topTiles = objectNode.attribute("TopTiles").as_int();

            isCappedN = objectNode.attribute("cappedN").as_bool();
            isCappedW = objectNode.attribute("cappedW").as_bool();
            isCappedE = objectNode.attribute("cappedE").as_bool();
            isCappedS = objectNode.attribute("cappedS").as_bool();

            halfDepth = roofDepth == Point5 || roofDepth == OneP5 || roofDepth == TwoP5;

            int width = objectNode.attribute("width").as_int();
            int height = objectNode.attribute("height").as_int();

            if (roofType >= CornerInnerSW && roofType <= CornerOuterSE)
            {
                width = height = std::max(width, height);
            }

            setWidth(width);
            setHeight(height);
        }

        void setWidth(int width)
        {
            switch (roofType)
            {
                case SlopeW:
                case SlopeE:
                {
                    this->width = std::clamp(width, 1, 3);

                    switch (this->width)
                    {
                        case 1:
                            roofDepth = halfDepth ? Point5 : One;
                            break;
                        case 2:
                            roofDepth = halfDepth ? OneP5 : Two;
                            break;
                        case 3:
                            roofDepth = halfDepth ? TwoP5 : Three;
                            break;
                    }
                    break;
                }

                case SlopeN:
                case SlopeS:
                case PeakWE:
                case DormerW:
                case DormerE:
                {
                    this->width = width;
                    break;
                }

                case FlatTop:
                {
                    this->width = width;
                    if (roofDepth == InvalidDepth) roofDepth = Zero;
                    break;
                }

                case PeakNS:
                case DormerN:
                case DormerS:
                {
                    this->width = width;

                    switch (this->width)
                    {
                        case 1:
                            roofDepth = Point5;
                            break;
                        case 2:
                            roofDepth = One;
                            break;
                        case 3:
                            roofDepth = OneP5;
                            break;
                        case 4:
                            roofDepth = Two;
                            break;
                        case 5:
                            roofDepth = TwoP5;
                            break;
                        default:
                            roofDepth = Three;
                            break;
                    }
                    break;
                }

                case ShallowSlopeW:
                case ShallowSlopeE:
                {
                    this->width = std::clamp(width, 1, 2);
                    roofDepth = Zero;
                    break;
                }

                case ShallowSlopeN:
                case ShallowSlopeS:
                case ShallowPeakWE:
                {
                    this->width = width;
                    roofDepth = Zero;
                    break;
                }

                case ShallowPeakNS:
                {
                    this->width = width < 4 ? 2 : 4;
                    this->roofDepth = Zero;
                    break;
                }

                case CornerInnerSW:
                case CornerInnerNW:
                case CornerInnerNE:
                case CornerInnerSE:
                case CornerOuterSW:
                case CornerOuterNW:
                case CornerOuterNE:
                case CornerOuterSE:
                {
                    this->width = std::clamp(width, 1, 3);

                    switch (this->width)
                    {
                        case 1:
                            roofDepth = halfDepth ? Point5 : One;
                            break;
                        case 2:
                            roofDepth = halfDepth ? OneP5 : Two;
                            break;
                        case 3:
                            roofDepth = halfDepth ? TwoP5 : Three;
                            break;
                    }
                    break;
                }

                default:
                    break;
            }
        }

        void setHeight(int height)
        {
            switch (roofType)
            {
                case SlopeW:
                case SlopeE:
                {
                    this->height = height;
                    break;
                }

                case SlopeN:
                case SlopeS:
                {
                    this->height = std::clamp(height, 1, 3);

                    switch (this->height)
                    {
                        case 1:
                            roofDepth = halfDepth ? Point5 : One;
                            break;
                        case 2:
                            roofDepth = halfDepth ? OneP5 : Two;
                            break;
                        case 3:
                            roofDepth = halfDepth ? TwoP5 : Three;
                            break;
                    }
                    break;
                }

                case PeakWE:
                case DormerW:
                case DormerE:
                {
                    this->height = height; //qBound(1, height, 6);
                    switch (this->height)
                    {
                        case 1:
                            roofDepth = Point5;
                            break;
                        case 2:
                            roofDepth = One;
                            break;
                        case 3:
                            roofDepth = OneP5;
                            break;
                        case 4:
                            roofDepth = Two;
                            break;
                        case 5:
                            roofDepth = TwoP5;
                            break;
                        default:
                            roofDepth = Three;
                            break;
                    }
                    if (roofType == DormerW || roofType == DormerE)
                    {
                        switch (roofDepth)
                        {
                            case OneP5:
                            case Two:
                                this->width = std::max(this->width, 2);
                                break;
                            case TwoP5:
                            case Three:
                                this->width = std::max(this->width, 3);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                }

                case PeakNS:
                {
                    this->height = height;
                    break;
                }

                case DormerN:
                case DormerS:
                {
                    this->height = height;
                    switch (roofDepth)
                    {
                        case OneP5:
                        case Two:
                            this->height = std::max(this->height, 2);
                            break;
                        case TwoP5:
                        case Three:
                            this->height = std::max(this->height, 3);
                            break;
                        default:
                            break;
                    }
                    break;
                }

                case FlatTop:
                {
                    this->height = height;
                    if (roofDepth == InvalidDepth)
                        roofDepth = Three;
                    break;
                }

                case ShallowSlopeW:
                case ShallowSlopeE:
                {
                    this->height = height;
                    roofDepth = Zero;
                    break;
                }

                case ShallowSlopeN:
                case ShallowSlopeS:
                {
                    this->height = std::clamp(height, 1, 2);
                    roofDepth = Zero;
                    break;
                }

                case ShallowPeakWE:
                {
                    if (height < 4)
                        this->height = 2;
                    else
                        this->height = 4;
                    roofDepth = Zero;
                    break;
                }

                case ShallowPeakNS:
                {
                    this->height = height;
                    roofDepth = Zero;
                    break;
                }

                case CornerInnerSW:
                case CornerInnerNW:
                case CornerInnerNE:
                case CornerInnerSE:
                case CornerOuterSW:
                case CornerOuterNW:
                case CornerOuterNE:
                case CornerOuterSE:
                {
                    this->height = std::clamp(height, 1, 3);

                    switch (this->height)
                    {
                        case 1:
                            roofDepth = halfDepth ? Point5 : One;
                            break;
                        case 2:
                            roofDepth = halfDepth ? OneP5 : Two;
                            break;
                        case 3:
                            roofDepth = halfDepth ? TwoP5 : Three;
                            break;
                    }
                    break;
                }

                default:
                    break;
            }
        }

        void setRoofType(const pugi::xml_node &node)
        {
            static const std::unordered_map<std::string_view, RoofType> typesLookup = {
                { "SlopeW", SlopeW },
                { "SlopeN", SlopeN },
                { "SlopeE", SlopeE },
                { "SlopeS", SlopeS },
                { "PeakWE", PeakWE },
                { "PeakNS", PeakNS },
                { "DormerW", DormerW },
                { "DormerN", DormerN },
                { "DormerE", DormerE },
                { "DormerS", DormerS },
                { "FlatTop", FlatTop },

                { "ShallowSlopeW", ShallowSlopeW },
                { "ShallowSlopeN", ShallowSlopeN },
                { "ShallowSlopeE", ShallowSlopeE },
                { "ShallowSlopeS", ShallowSlopeS },
                { "ShallowPeakWE", ShallowPeakWE },
                { "ShallowPeakNS", ShallowPeakNS },

                { "CornerInnerSW", CornerInnerSW },
                { "CornerInnerNW", CornerInnerNW },
                { "CornerInnerNE", CornerInnerNE },
                { "CornerInnerSE", CornerInnerSE },
                { "CornerOuterSW", CornerOuterSW },
                { "CornerOuterNW", CornerOuterNW },
                { "CornerOuterNE", CornerOuterNE },
                { "CornerOuterSE", CornerOuterSE }
            };

            auto it = typesLookup.find(node.attribute("RoofType").value());
            if (it != typesLookup.end())
            {
                roofType = it->second;
            }
        }

        void setRoofDepth(const pugi::xml_node &node)
        {
            static const std::unordered_map<std::string_view, Depth> depthLookup = {
                { "Zero", Zero },
                { "Point5", Point5 },
                { "One", One },
                { "OnePoint5", OneP5 },
                { "Two", Two },
                { "TwoPoint5", TwoP5 },
                { "Three", Three },
            };

            auto it = depthLookup.find(node.attribute("Depth").as_string());
            if (it != depthLookup.end())
            {
                roofDepth = it->second;
            }
        }

        static const std::string tileToName(const RoofTile tile)
        {
            static const std::unordered_map<RoofTile, const std::string> lookup = {
                { RoofTile::SlopeS1, "SlopeS1" },
                { RoofTile::SlopeS2, "SlopeS2" },
                { RoofTile::SlopeS3, "SlopeS3" },
                { RoofTile::SlopeE1, "SlopeE1" },
                { RoofTile::SlopeE2, "SlopeE2" },
                { RoofTile::SlopeE3, "SlopeE3" },
                { RoofTile::SlopePt5S, "SlopePt5S" },
                { RoofTile::SlopePt5E, "SlopePt5E" },
                { RoofTile::SlopeOnePt5S, "SlopeOnePt5S" },
                { RoofTile::SlopeOnePt5E, "SlopeOnePt5E" },
                { RoofTile::SlopeTwoPt5S, "SlopeTwoPt5S" },
                { RoofTile::SlopeTwoPt5E, "SlopeTwoPt5E" },

                // Shallow sides
                { RoofTile::ShallowSlopeW1, "ShallowSlopeW1" },
                { RoofTile::ShallowSlopeW2, "ShallowSlopeW2" },
                { RoofTile::ShallowSlopeE1, "ShallowSlopeE1" },
                { RoofTile::ShallowSlopeE2, "ShallowSlopeE2" },
                { RoofTile::ShallowSlopeN1, "ShallowSlopeN1" },
                { RoofTile::ShallowSlopeN2, "ShallowSlopeN2" },
                { RoofTile::ShallowSlopeS1, "ShallowSlopeS1" },
                { RoofTile::ShallowSlopeS2, "ShallowSlopeS2" },

                // Corners
                { RoofTile::Inner1, "Inner1" },
                { RoofTile::Inner2, "Inner2" },
                { RoofTile::Inner3, "Inner3" },
                { RoofTile::Outer1, "Outer1" },
                { RoofTile::Outer2, "Outer2" },
                { RoofTile::Outer3, "Outer3" },
                { RoofTile::InnerPt5, "InnerPt5" },
                { RoofTile::InnerOnePt5, "InnerOnePt5" },
                { RoofTile::InnerTwoPt5, "InnerTwoPt5" },
                { RoofTile::OuterPt5, "OuterPt5" },
                { RoofTile::OuterOnePt5, "OuterOnePt5" },
                { RoofTile::OuterTwoPt5, "OuterTwoPt5" },
                { RoofTile::CornerSW1, "CornerSW1" },
                { RoofTile::CornerSW2, "CornerSW2" },
                { RoofTile::CornerSW3, "CornerSW3" },
                { RoofTile::CornerNE1, "CornerNE1" },
                { RoofTile::CornerNE2, "CornerNE2" },
                { RoofTile::CornerNE3, "CornerNE3" },

                // Caps
                { RoofTile::CapRiseE1, "CapRiseE1" },
                { RoofTile::CapRiseE2, "CapRiseE2" },
                { RoofTile::CapRiseE3, "CapRiseE3" },
                { RoofTile::CapFallE1, "CapFallE1" },
                { RoofTile::CapFallE2, "CapFallE2" },
                { RoofTile::CapFallE3, "CapFallE3" },
                { RoofTile::CapRiseS1, "CapRiseS1" },
                { RoofTile::CapRiseS2, "CapRiseS2" },
                { RoofTile::CapRiseS3, "CapRiseS3" },
                { RoofTile::CapFallS1, "CapFallS1" },
                { RoofTile::CapFallS2, "CapFallS2" },
                { RoofTile::CapFallS3, "CapFallS3" },
                { RoofTile::PeakPt5S, "PeakPt5S" },
                { RoofTile::PeakPt5E, "PeakPt5E" },
                { RoofTile::PeakOnePt5S, "PeakOnePt5S" },
                { RoofTile::PeakOnePt5E, "PeakOnePt5E" },
                { RoofTile::PeakTwoPt5S, "PeakTwoPt5S" },
                { RoofTile::PeakTwoPt5E, "PeakTwoPt5E" },
                { RoofTile::CapGapS1, "CapGapS1" },
                { RoofTile::CapGapS2, "CapGapS2" },
                { RoofTile::CapGapS3, "CapGapS3" },
                { RoofTile::CapGapE1, "CapGapE1" },
                { RoofTile::CapGapE2, "CapGapE2" },
                { RoofTile::CapGapE3, "CapGapE3" },

                // Cap tiles for shallow roofs
                { RoofTile::CapShallowRiseS1, "CapShallowRiseS1" },
                { RoofTile::CapShallowRiseS2, "CapShallowRiseS2" },
                { RoofTile::CapShallowFallS1, "CapShallowFallS1" },
                { RoofTile::CapShallowFallS2, "CapShallowFallS2" },
                { RoofTile::CapShallowRiseE1, "CapShallowRiseE1" },
                { RoofTile::CapShallowRiseE2, "CapShallowRiseE2" },
                { RoofTile::CapShallowFallE1, "CapShallowFallE1" },
                { RoofTile::CapShallowFallE2, "CapShallowFallE2" }
            };

            if (!lookup.contains(tile))
            {
                logger.error("RoofTile not found: '{}'", magic_enum::enum_name(tile));
            }

            return lookup.at(tile);
        }

        Rectangle2D getBounds() const
        {
            return Rectangle2D(x, y, width, height);
        }

        Rectangle2D flatTop() const
        {
            Rectangle2D r = getBounds();

            if (roofType == FlatTop)
                return r;

            if ((roofType == PeakWE || roofType == DormerW || roofType == DormerE) && height > 6)
                return Rectangle2D(r.left(), r.top() + 3, r.width, r.height - 6);

            if ((roofType == PeakNS || roofType == DormerN || roofType == DormerS) && height > 6)
                return Rectangle2D(r.left() + 3, r.top(), r.width - 6, r.height);

            return Rectangle2D();
        }

        int slopeThickness() const
        {
            switch (roofType)
            {
                case PeakWE:
                case DormerW:
                case DormerE:
                    return std::ceil(std::min(height, 6) / 2.f);

                case PeakNS:
                case DormerN:
                case DormerS:
                    return std::ceil(std::min(width, 6) / 2.f);

                default:
                    return 0;
            }
        }

        std::vector<RoofTile> repeat(Direction dir, const std::vector<RoofTile> &vec) const
        {
            std::vector<RoofTile> result;

            Assert::isTrue(width > 0);
            Assert::isTrue(height > 0);

            switch (dir)
            {
                // v0, v1, v2 ...
                // v0, v1, v2 ...
                // v0, v1, v2 ...
                case Direction::W:
                {
                    result.reserve(vec.size() * height);

                    for (int i = 0; i < height; i++)
                    {
                        result.insert(result.end(), vec.begin(), vec.end());
                    }
                    break;
                }

                // v0, v0, v0 ...
                // v1, v1, v1 ...
                // v2, v2, v2 ...
                case Direction::N:
                {
                    result.reserve(vec.size() * width);

                    for (const RoofTile &tile : vec)
                    {
                        result.insert(result.end(), width, tile);
                    }
                    break;
                }

                default:
                    throw std::runtime_error(std::format("Invalid direction: '{}'", magic_enum::enum_name(dir)));
            }

            return result;
        }

        std::vector<RoofTile> getSlopeTiles(Rectangle2D &rect) const
        {
            rect = getBounds();

            // clang-format off
            static const std::unordered_map<RoofType, std::unordered_map<Depth, std::vector<RoofTile>>> patterns = {
                {
                    RoofType::SlopeE, {
                        { Three,  { SlopeE3, SlopeE2, SlopeE1 }},
                        { TwoP5,  { SlopeTwoPt5E, SlopeE2, SlopeE1 }},
                        { Two,    { SlopeE2, SlopeE1 }},
                        { OneP5,  { SlopeOnePt5E, SlopeE1 }},
                        { One,    { SlopeE1 }},
                        { Point5, { SlopePt5E }},
                    },
                },
                {
                    RoofType::SlopeS, {
                        { Three,  { SlopeS3, SlopeS2, SlopeS1 }},
                        { TwoP5,  { SlopeTwoPt5S, SlopeS2, SlopeS1 }},
                        { Two,    { SlopeS2, SlopeS1 }},
                        { OneP5,  { SlopeOnePt5S, SlopeS1 }},
                        { One,    { SlopeS1 }},
                        { Point5, { SlopePt5S }},
                    },
                }
            }; // clang-format on

            switch (roofType)
            {
                case SlopeE:
                {
                    return repeat(Direction::W, patterns.at(SlopeE).at(roofDepth));
                }
                case SlopeS:
                {
                    return repeat(Direction::N, patterns.at(SlopeS).at(roofDepth));
                }
                case PeakWE:
                {
                    rect.x = rect.left();
                    rect.y = rect.bottom() - slopeThickness();
                    rect.width = rect.width;
                    rect.height = slopeThickness();

                    return repeat(Direction::N, patterns.at(SlopeS).at(roofDepth));
                }
                case PeakNS:
                {
                    rect.x = rect.right() - slopeThickness();
                    rect.y = rect.top();
                    rect.width = slopeThickness();
                    rect.height = rect.height;

                    return repeat(Direction::W, patterns.at(SlopeE).at(roofDepth));
                }
                case DormerW:
                {
                    rect.x = rect.left();
                    rect.y = rect.bottom() - slopeThickness();
                    rect.width = rect.width - slopeThickness();
                    rect.height = slopeThickness();

                    return repeat(Direction::N, patterns.at(SlopeS).at(roofDepth));
                }
                case DormerE:
                {
                    rect.x = rect.left() + slopeThickness();
                    rect.y = rect.bottom() - slopeThickness() + 1;
                    rect.width = rect.width - slopeThickness();
                    rect.height = slopeThickness();

                    return repeat(Direction::N, patterns.at(SlopeS).at(roofDepth));
                }
                case DormerN:
                {
                    rect.x = rect.right() - slopeThickness();
                    rect.y = rect.top();
                    rect.width = slopeThickness();
                    rect.height = rect.height - slopeThickness();

                    return repeat(Direction::W, patterns.at(SlopeE).at(roofDepth));
                }
                case DormerS:
                {
                    rect.x = rect.right() - slopeThickness();
                    rect.y = rect.top() + slopeThickness();
                    rect.width = slopeThickness();
                    rect.height = rect.height - slopeThickness();

                    return repeat(Direction::W, patterns.at(SlopeE).at(roofDepth));
                }
                case ShallowSlopeW:
                {
                    if (width > 1)
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeW1, ShallowSlopeW2 });
                    }
                    else
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeW1 });
                    }
                }
                case ShallowSlopeE:
                {
                    if (width > 1)
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeE2, ShallowSlopeE1 });
                    }
                    else
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeE1 });
                    }
                }
                case ShallowSlopeN:
                {
                    if (height > 1)
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeN1, ShallowSlopeN2 });
                    }
                    else
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeN1 });
                    }
                }
                case ShallowSlopeS:
                {
                    if (height > 1)
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeS2, ShallowSlopeS1 });
                    }
                    else
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeS1 });
                    }
                }
                case ShallowPeakNS:
                {
                    if (width > 2)
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeW1, ShallowSlopeW2, ShallowSlopeE2, ShallowSlopeE1 });
                    }
                    else
                    {
                        return repeat(Direction::W, std::vector<RoofTile>{ ShallowSlopeW1, ShallowSlopeE1 });
                    }
                }
                case ShallowPeakWE:
                {
                    if (height > 2)
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeN1, ShallowSlopeN2, ShallowSlopeS2, ShallowSlopeS1 });
                    }
                    else
                    {
                        return repeat(Direction::N, std::vector<RoofTile>{ ShallowSlopeN1, ShallowSlopeS1 });
                    }
                }
                default:
                    return {};
            }
        }

        std::vector<RoofTile> getWestCapTiles(Rectangle2D &rect) const
        {
            if (!isCappedW) return {};

            Rectangle2D bounds = this->getBounds();

            rect.x = bounds.x;
            rect.y = bounds.y;
            rect.width = 1;
            rect.height = bounds.height;

            switch (roofType) // clang-format off
            {
                case PeakWE:
                case DormerW:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            std::vector<RoofTile> result;

                            result.insert(result.end(), { CapFallE1, CapFallE2, CapFallE3 });
                            result.insert(result.end(), std::max(0, height - 6), CapGapE3);
                            result.insert(result.end(), { CapRiseE3, CapRiseE2, CapRiseE1 });

                            return result;
                        }
                        case TwoP5:  return { CapFallE1, CapFallE2, PeakTwoPt5E, CapRiseE2, CapRiseE1 };
                        case Two:    return { CapFallE1, CapFallE2, CapRiseE2, CapRiseE1 };
                        case OneP5:  return { CapFallE1, PeakOnePt5E, CapRiseE1 };
                        case One:    return { CapFallE1, CapRiseE1 };
                        case Point5: return { PeakPt5E };
                        default:     break;

                    }
                }

                case SlopeN:
                case CornerOuterNE:
                case CornerInnerSE:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapFallE1 , CapFallE2 , CapFallE3 };
                        }
                        case TwoP5:
                        {
                            rect.height--;
                            return { CapFallE1 , CapFallE2 };
                        }
                        case Two:
                        {
                            return { CapFallE1 , CapFallE2 };
                        }
                        case OneP5:
                        {
                            rect.height--;
                            return { CapFallE1 };
                        }
                        case One:
                        {
                            return { CapFallE1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case SlopeS:
                case CornerInnerNE:
                case CornerOuterSE:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapRiseE3 , CapRiseE2 , CapRiseE1 };
                        }
                        case TwoP5:
                        {
                            rect.y++;
                            return { CapRiseE2 , CapRiseE1 };
                        }
                        case Two:
                        {
                            return { CapRiseE2 , CapRiseE1 };
                        }
                        case OneP5:
                        {
                            rect.y++;
                            return { CapRiseE1 };
                        }
                        case One:
                        {
                            return { CapRiseE1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case ShallowSlopeN:
                {
                    if(height > 1)
                    {
                        return { CapShallowFallE1, CapShallowFallE2 };
                    }
                    else
                    {
                        return { CapShallowFallE1 };
                    }
                }

                case ShallowSlopeS:
                {
                    if(height > 1)
                    {
                        return { CapShallowRiseE2, CapShallowRiseE1 };
                    }
                    else
                    {
                        return { CapShallowRiseE1 };
                    }
                }

                case ShallowPeakWE:
                {
                    if(height > 2)
                    {
                        return { CapShallowFallE1, CapShallowFallE2 , CapShallowRiseE2, CapShallowRiseE1 };
                    }
                    else
                    {
                        return { CapShallowFallE1, CapShallowRiseE1 };
                    }
                }

                case FlatTop:
                case CornerInnerSW:
                case CornerInnerNW:
                {
                    switch (roofDepth)
                    {
                        case Three:  return std::vector<RoofTile>(height, CapGapE3);
                        case TwoP5:  return std::vector<RoofTile>(height, CapGapE3);
                        case Two:    return std::vector<RoofTile>(height, CapGapE2);
                        case OneP5:  return std::vector<RoofTile>(height, CapGapE3);
                        case One:    return std::vector<RoofTile>(height, CapGapE1);
                        case Point5: return std::vector<RoofTile>(height, CapGapE3);
                        default:     break;
                    }
                }

                default:
                    break;
            } // clang-format on

            return {};
        }

        std::vector<RoofTile> getEastCapTiles(Rectangle2D &rect) const
        {
            if (!isCappedE) return {};

            Rectangle2D bounds = this->getBounds();

            rect.x = bounds.right();
            rect.y = bounds.top();
            rect.width = 1;
            rect.height = bounds.height;

            switch (roofType) // clang-format off
            {
                case PeakWE:
                case DormerE:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            std::vector<RoofTile> result;

                            result.insert(result.end(), { CapFallE1, CapFallE2, CapFallE3 });
                            result.insert(result.end(), std::max(0, height - 6), CapGapE3);
                            result.insert(result.end(), { CapRiseE3, CapRiseE2, CapRiseE1 });

                            return result;
                        }
                        case TwoP5:  return { CapFallE1, CapFallE2, PeakTwoPt5E, CapRiseE2, CapRiseE1 };
                        case Two:    return { CapFallE1, CapFallE2, CapRiseE2, CapRiseE1 };
                        case OneP5:  return { CapFallE1, PeakOnePt5E, CapRiseE1 };
                        case One:    return { CapFallE1, CapRiseE1 };
                        case Point5: return { PeakPt5E };
                        default:     break;

                    }
                }

                case SlopeN:
                case CornerInnerSW:
                case CornerOuterNW:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapFallE1 , CapFallE2 , CapFallE3 };
                        }
                        case TwoP5:
                        {
                            rect.height--;
                            return { CapFallE1 , CapFallE2 };
                        }
                        case Two:
                        {
                            return { CapFallE1 , CapFallE2 };
                        }
                        case OneP5:
                        {
                            rect.height--;
                            return { CapFallE1 };
                        }
                        case One:
                        {
                            return { CapFallE1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case SlopeS:
                case CornerInnerNW:
                case CornerOuterSW:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapRiseE3 , CapRiseE2 , CapRiseE1 };
                        }
                        case TwoP5:
                        {
                            rect.y++;
                            return { CapRiseE2 , CapRiseE1 };
                        }
                        case Two:
                        {
                            return { CapRiseE2 , CapRiseE1 };
                        }
                        case OneP5:
                        {
                            rect.y++;
                            return { CapRiseE1 };
                        }
                        case One:
                        {
                            return { CapRiseE1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case ShallowSlopeN:
                {
                    if(height > 1)
                    {
                        return { CapShallowFallE1, CapShallowFallE2 };
                    }
                    else
                    {
                        return { CapShallowFallE1 };
                    }
                }

                case ShallowSlopeS:
                {
                    if(height > 1)
                    {
                        return { CapShallowRiseE2, CapShallowRiseE1 };
                    }
                    else
                    {
                        return { CapShallowRiseE1 };
                    }
                }

                case ShallowPeakWE:
                {
                    if(height > 2)
                    {
                        return { CapShallowFallE1, CapShallowFallE2 , CapShallowRiseE2, CapShallowRiseE1 };
                    }
                    else
                    {
                        return { CapShallowFallE1, CapShallowRiseE1 };
                    }
                }

                case FlatTop:
                case CornerInnerSE:
                case CornerInnerNE:
                {
                    switch (roofDepth)
                    {
                        case Three:  return std::vector<RoofTile>(height, CapGapE3);
                        case TwoP5:  return std::vector<RoofTile>(height, CapGapE3);
                        case Two:    return std::vector<RoofTile>(height, CapGapE2);
                        case OneP5:  return std::vector<RoofTile>(height, CapGapE3);
                        case One:    return std::vector<RoofTile>(height, CapGapE1);
                        case Point5: return std::vector<RoofTile>(height, CapGapE3);
                        default:     break;
                    }
                }

                default:
                    break;
            } // clang-format on

            return {};
        }

        std::vector<RoofTile> getNorthCapTiles(Rectangle2D &rect) const
        {
            if (!isCappedN) return {};

            Rectangle2D bounds = this->getBounds();

            rect.x = bounds.x;
            rect.y = bounds.top();
            rect.width = bounds.width;
            rect.height = 1;

            switch (roofType) // clang-format off
            {
                case PeakNS:
                case DormerN:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            std::vector<RoofTile> result;

                            result.insert(result.end(), { CapRiseS1, CapRiseS2, CapRiseS3 });
                            result.insert(result.end(), std::max(0, width - 6), CapGapS3);
                            result.insert(result.end(), { CapFallS3, CapFallS2, CapFallS1 });

                            return result;
                        }
                        case TwoP5:  return { CapRiseS1, CapRiseS2, PeakTwoPt5S, CapFallS2, CapFallS1 };
                        case Two:    return { CapRiseS1, CapRiseS2, CapFallS2, CapFallS1 };
                        case OneP5:  return { CapRiseS1, PeakOnePt5S, CapFallS1 };
                        case One:    return { CapRiseS1, CapFallS1 };
                        case Point5: return { PeakPt5S };
                        default:     break;

                    }
                }

                case SlopeW:
                case CornerInnerSE:
                case CornerOuterSW:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapRiseS1, CapRiseS2, CapRiseS3 };
                        }
                        case TwoP5:
                        {
                            rect.width--;
                            return { CapRiseS1, CapRiseS2 };
                        }
                        case Two:
                        {
                            return { CapRiseS1, CapRiseS2 };
                        }
                        case OneP5:
                        {
                            rect.width--;
                            return { CapRiseS1 };
                        }
                        case One:
                        {
                            return { CapRiseS1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case SlopeE:
                case CornerInnerSW:
                case CornerOuterSE:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapFallS3, CapFallS2, CapFallS1 };
                        }
                        case TwoP5:
                        {
                            rect.x++;
                            return { CapFallS2, CapFallS1 };
                        }
                        case Two:
                        {
                            return { CapFallS2, CapFallS1 };
                        }
                        case OneP5:
                        {
                            rect.x++;
                            return { CapFallS1 };
                        }
                        case One:
                        {
                            return { CapFallS1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case ShallowSlopeW:
                {
                    if(height > 1)
                    {
                        return { CapShallowRiseS1, CapShallowRiseS2 };
                    }
                    else
                    {
                        return { CapShallowRiseS1 };
                    }
                }

                case ShallowSlopeE:
                {
                    if(height > 1)
                    {
                        return { CapShallowFallS2, CapShallowFallS1 };
                    }
                    else
                    {
                        return { CapShallowFallS1 };
                    }
                }

                case ShallowPeakNS:
                {
                    if(height > 2)
                    {
                        return { CapShallowRiseS1, CapShallowRiseS2, CapShallowFallS2, CapShallowFallS1 };
                    }
                    else
                    {
                        return { CapShallowRiseS1, CapShallowFallS1 };
                    }
                }

                case FlatTop:
                case CornerInnerNW:
                case CornerInnerNE:
                {
                    switch (roofDepth)
                    {
                        case Three:  return std::vector<RoofTile>(width, CapGapS3);
                        case TwoP5:  return std::vector<RoofTile>(width, CapGapS3);
                        case Two:    return std::vector<RoofTile>(width, CapGapS2);
                        case OneP5:  return std::vector<RoofTile>(width, CapGapS3);
                        case One:    return std::vector<RoofTile>(width, CapGapS1);
                        case Point5: return std::vector<RoofTile>(width, CapGapS3);
                        default:     break;
                    }
                }

                default:
                    break;
            } // clang-format on

            return {};
        }

        std::vector<RoofTile> getSouthCapTiles(Rectangle2D &rect) const
        {
            if (!isCappedS) return {};

            Rectangle2D bounds = this->getBounds();

            rect.x = bounds.x;
            rect.y = bounds.bottom();
            rect.width = bounds.width;
            rect.height = 1;

            switch (roofType) // clang-format off
            {
                case PeakNS:
                case DormerS:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            std::vector<RoofTile> result;

                            result.insert(result.end(), { CapRiseS1, CapRiseS2, CapRiseS3 });
                            result.insert(result.end(), std::max(0, width - 6), CapGapS3);
                            result.insert(result.end(), { CapFallS3, CapFallS2, CapFallS1 });

                            return result;
                        }
                        case TwoP5:  return { CapRiseS1, CapRiseS2, PeakTwoPt5S, CapFallS2, CapFallS1 };
                        case Two:    return { CapRiseS1, CapRiseS2, CapFallS2, CapFallS1 };
                        case OneP5:  return { CapRiseS1, PeakOnePt5S, CapFallS1 };
                        case One:    return { CapRiseS1, CapFallS1 };
                        case Point5: return { PeakPt5S };
                        default:     break;

                    }
                }

                case SlopeW:
                case CornerInnerNE:
                case CornerOuterNW:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapRiseS1, CapRiseS2, CapRiseS3 };
                        }
                        case TwoP5:
                        {
                            rect.width--;
                            return { CapRiseS1, CapRiseS2 };
                        }
                        case Two:
                        {
                            return { CapRiseS1, CapRiseS2 };
                        }
                        case OneP5:
                        {
                            rect.width--;
                            return { CapRiseS1 };
                        }
                        case One:
                        {
                            return { CapRiseS1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case SlopeE:
                case CornerInnerNW:
                case CornerOuterNE:
                {
                    switch (roofDepth)
                    {
                        case Three:
                        {
                            return { CapFallS3, CapFallS2, CapFallS1 };
                        }
                        case TwoP5:
                        {
                            rect.x++;
                            return { CapFallS2, CapFallS1 };
                        }
                        case Two:
                        {
                            return { CapFallS2, CapFallS1 };
                        }
                        case OneP5:
                        {
                            rect.x++;
                            return { CapFallS1 };
                        }
                        case One:
                        {
                            return { CapFallS1 };
                        }
                        case Point5:
                        {
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                }

                case ShallowSlopeW:
                {
                    if(width > 1)
                    {
                        return { CapShallowRiseS1, CapShallowRiseS2 };
                    }
                    else
                    {
                        return { CapShallowRiseS1 };
                    }
                }

                case ShallowSlopeE:
                {
                    if(width > 1)
                    {
                        return { CapShallowFallS2, CapShallowFallS1 };
                    }
                    else
                    {
                        return { CapShallowFallS1 };
                    }
                }

                case ShallowPeakNS:
                {
                    if(width > 2)
                    {
                        return { CapShallowRiseS1, CapShallowRiseS2, CapShallowFallS2, CapShallowFallS1 };
                    }
                    else
                    {
                        return { CapShallowRiseS1, CapShallowFallS1 };
                    }
                }

                case FlatTop:
                case CornerInnerSE:
                case CornerInnerSW:
                {
                    switch (roofDepth)
                    {
                        case Three:  return std::vector<RoofTile>(width, CapGapS3);
                        case TwoP5:  return std::vector<RoofTile>(width, CapGapS3);
                        case Two:    return std::vector<RoofTile>(width, CapGapS2);
                        case OneP5:  return std::vector<RoofTile>(width, CapGapS3);
                        case One:    return std::vector<RoofTile>(width, CapGapS1);
                        case Point5: return std::vector<RoofTile>(width, CapGapS3);
                        default:     break;
                    }
                }

                default:
                    break;
            } // clang-format on

            return {};
        }

        std::vector<RoofTile> getCornerTiles(Rectangle2D &rect) const
        {
            rect = getBounds();

            // clang-format off
            switch (roofType)
            {
                case DormerE:
                {
                    rect.x = rect.left();
                    rect.y = rect.bottom() - slopeThickness();
                    rect.width  = slopeThickness();
                    rect.height = slopeThickness();

                    switch(roofDepth)
                    {
                        case Three:  return { Inner3, SlopeS3, SlopeS3, TileCount, Inner2, SlopeS2, TileCount, TileCount, Inner1 };
                        case TwoP5:  return { InnerTwoPt5, SlopeTwoPt5S, SlopeTwoPt5S, TileCount, Inner2, SlopeS2, TileCount, TileCount, Inner1 };
                        case Two:    return { Inner2, SlopeS2, TileCount, Inner1 };
                        case OneP5:  return { InnerOnePt5, SlopeOnePt5S, TileCount, Inner1 };
                        case One:    return { Inner1 };
                        case Point5: return { InnerPt5 };
                        default:     break;
                    }
                }
                case DormerS:
                {
                    rect.x = rect.right() - slopeThickness();
                    rect.y = rect.top();
                    rect.width = slopeThickness();
                    rect.height = slopeThickness();

                    switch(roofDepth)
                    {
                        case Three:  return { Inner3, TileCount, TileCount, SlopeE3, Inner2, TileCount, SlopeE3, SlopeE2, Inner1 };
                        case TwoP5:  return { InnerTwoPt5, TileCount, TileCount, SlopeTwoPt5E, Inner2, TileCount, SlopeTwoPt5E, SlopeE2, Inner1 };
                        case Two:    return { Inner2, TileCount, SlopeE2, Inner1 };
                        case OneP5:  return { InnerOnePt5, TileCount, SlopeOnePt5E, Inner1 };
                        case One:    return { Inner1 };
                        case Point5: return { InnerPt5 };
                        default:     break;
                    }
                }
                case CornerInnerNW:
                {
                    switch(roofDepth)
                    {
                        case Three:  return { Inner3, SlopeS3, SlopeS3, SlopeE3, Inner2, SlopeS2, SlopeE3, SlopeE2, Inner1 };
                        case TwoP5:  return { InnerTwoPt5, SlopeTwoPt5S, SlopeTwoPt5S, SlopeTwoPt5E, Inner2, SlopeS2, SlopeTwoPt5E, SlopeE2, Inner1 };
                        case Two:    return { Inner2, SlopeS2, SlopeE2, Inner1 };
                        case OneP5:  return { InnerOnePt5, SlopeOnePt5S, SlopeOnePt5E, Inner1 };
                        case One:    return { Inner1 };
                        case Point5: return { InnerPt5 };
                        default:     break;
                    }
                }
                case CornerOuterSW:
                {
                    switch(roofDepth)
                    {
                        case Three:  return { TileCount, TileCount, CornerSW3, TileCount, CornerSW2, SlopeS2, CornerSW1, SlopeS1, SlopeS1 };
                        case Two:    return { TileCount, CornerSW2, CornerSW1, SlopeS1 };
                        case One:    return { CornerSW1 };
                        default:     break;
                    }
                }
                case CornerOuterNE:
                {
                    switch(roofDepth)
                    {
                        case Three:  return { TileCount, TileCount, CornerNE1, TileCount, CornerNE2, SlopeE1, CornerNE3, SlopeE2, SlopeE1 };
                        case Two:    return { TileCount, CornerNE1, CornerNE2, SlopeE1 };
                        case One:    return { CornerNE1 };
                        default:     break;
                    }
                }
                case CornerOuterSE:
                {
                    switch(roofDepth)
                    {
                        case Three:  return { Outer3, SlopeE2, SlopeE1, SlopeS2, Outer2, SlopeE1, SlopeS1, SlopeS1, Outer1 };
                        case TwoP5:  return { OuterTwoPt5, SlopeE2, SlopeE1, SlopeS2, Outer2, SlopeE1, SlopeS1, SlopeS1, Outer1 };
                        case Two:    return { Outer2, SlopeE1, SlopeS1, Outer1 };
                        case OneP5:  return { OuterOnePt5, SlopeE1, SlopeS1, Outer1 };
                        case One:    return { Outer1 };
                        case Point5: return { OuterPt5 };
                        default:     break;
                    }
                }

                case CornerOuterNW:
                case CornerInnerNE:
                case CornerInnerSW:
                case CornerInnerSE:
                    break;

                default:
                    break;
            }
            // clang-format on

            return {};
        }

        RoofObject *asRoof() override { return this; }
    };

    struct WallObject : public BuildingObject
    {
        int32_t tile = 0;
        int32_t length = 0;
        int32_t interiorTile = 0;
        int32_t interiorTrim = 0;
        int32_t exteriorTrim = 0;

        WallObject *asWall() override { return this; }
    };

    struct Square
    {
        enum Section : int8_t
        {
            SectionInvalid = -1,
            SectionFloor,
            SectionFloorGrime,
            SectionFloorGrime2,
            SectionFloorFurniture,
            SectionWall,
            SectionWallTrim,
            SectionWall2,
            SectionWallTrim2,
            SectionRoofCap,
            SectionRoofCap2,
            SectionWallOverlay,  // West, North walls
            SectionWallOverlay2, // West, North walls
            SectionWallGrime,
            SectionWallGrime2,
            SectionWallFurniture,  // West, North walls
            SectionWallFurniture2, // West, North walls
            SectionFrame,
            SectionDoor,
            SectionWindow,
            SectionCurtains, // West, North windows
            SectionFurniture,
            SectionFurniture2,
            SectionFurniture3,
            SectionFurniture4,
            SectionCurtains2,      // East, South windows
            SectionWallFurniture3, // East, South walls
            SectionWallFurniture4, // East, South walls
            SectionWallOverlay3,   // East, South walls
            SectionWallOverlay4,   // East, South walls
            SectionRoof,
            SectionRoof2,
            SectionRoofTop,
            MaxSection
        };

        struct WallInfo
        {
            const BuildingTileEntry *wall = nullptr;
            const BuildingTileEntry *trim = nullptr;
            const BuildingTileEntry *grime = nullptr;

            const FurnitureTile::Tile *furnitureTile = nullptr;
            const TileDefinition::TileData *userTile = nullptr;

            std::string _enum;
            bool allowGrime = true;

            std::string _enumNW()
            {
                return "NorthWest";
            }
        };

        static inline const Section sectionWallW = SectionWall2;
        static inline const Section sectionWallN = SectionWall;

        static inline const Section sectionWallTrimW = SectionWallTrim2;
        static inline const Section sectionWallTrimN = SectionWallTrim;

        static inline const Section sectionWallGrimeW = SectionWallGrime;
        static inline const Section sectionWallGrimeN = SectionWallGrime2;

        static inline const Section sectionFloorGrimeW = SectionFloorGrime;
        static inline const Section sectionFloorGrimeN = SectionFloorGrime2;

        WallInfo wallN;
        WallInfo wallW;

        bool allowFloor = true;

        std::array<const TileDefinition::TileData *, MaxSection> tiles{};

        Square()
        {
            wallN._enum = "North";
            wallW._enum = "West";
        }

        void setTile(Section section, const BuildingTileEntry *entry, const std::string &_enum)
        {
            const TileDefinition::TileData *tileData = nullptr;

            if (entry != nullptr)
            {
                auto it = entry->tiles.find(_enum);
                if (it != entry->tiles.end())
                {
                    tileData = it->second.tileData;
                }
            }

            tiles[section] = tileData;
        }

        void setTile(const BuildingTileEntry *tileEntry, const std::string &_enum, Section sectionMin, Section sectionMax)
        {
            auto tileIt = tileEntry->tiles.find(_enum);
            if (tileIt != tileEntry->tiles.end())
            {
                setTile(tileIt->second.tileData, sectionMin, sectionMax);
            }
        }

        void setTile(const TileDefinition::TileData *tile, Section sectionMin, Section sectionMax)
        {
            for (uint8_t s = sectionMin; s < sectionMax; s++)
            {
                Section section = static_cast<Section>(s);

                if (tiles[section] == nullptr)
                {
                    tiles[section] = tile;
                    return;
                }
            }

            for (uint8_t s = sectionMin; s < sectionMax; s++)
            {
                tiles[static_cast<Section>(s)] = tiles[static_cast<Section>(s + 1)];
            }

            tiles[sectionMax] = tile;
        }

        void setFloor(const BuildingTileEntry *floor, const std::string &_enum = "Floor")
        {
            if (allowFloor)
            {
                setTile(SectionFloor, floor, _enum);
            }
        }

        void setFloorGrime(const BuildingTileEntry *grimeFloor)
        {
            if (hasWallW()) setTile(sectionFloorGrimeW, grimeFloor, "West");
            if (hasWallN()) setTile(sectionFloorGrimeN, grimeFloor, "North");
        }

        bool hasTileDefined(Section section) const
        {
            return tiles[section] != nullptr;
        }

        bool hasWallN() const
        {
            return wallN.wall != nullptr;
        }

        bool hasWallW() const
        {
            return wallW.wall != nullptr;
        }

        bool hasDoor() const
        {
            return hasTileDefined(SectionDoor) || hasTileDefined(SectionFrame);
        }

        bool hasWindow() const
        {
            return hasTileDefined(SectionWindow);
        }

        void setWallN(const BuildingTileEntry *wall, const BuildingTileEntry *trim, const BuildingTileEntry *grime)
        {
            if (!wall)
            {
                trim = nullptr;
                grime = nullptr;
            }

            wallN.wall = wall;
            wallN.trim = trim;
            wallN.grime = grime;
        }

        void setWallW(const BuildingTileEntry *wall, const BuildingTileEntry *trim, const BuildingTileEntry *grime)
        {
            if (!wall)
            {
                trim = nullptr;
                grime = nullptr;
            }

            wallW.wall = wall;
            wallW.trim = trim;
            wallW.grime = grime;
        }

        void setDoorN(const BuildingTileEntry *doorEntry, const BuildingTileEntry *frameEntry)
        {
            wallN._enum = "NorthDoor";
            setTile(SectionDoor, doorEntry, "North");
            setTile(SectionFrame, frameEntry, "North");
        }

        void setDoorW(const BuildingTileEntry *doorEntry, const BuildingTileEntry *frameEntry)
        {
            wallW._enum = "WestDoor";
            setTile(SectionDoor, doorEntry, "West");
            setTile(SectionFrame, frameEntry, "West");
        }

        void setWindowN(const BuildingTileEntry *window)
        {
            wallN._enum = "NorthWindow";
            setTile(SectionWindow, window, "North");
        }

        void setWindowW(const BuildingTileEntry *window)
        {
            wallW._enum = "WestWindow";
            setTile(SectionWindow, window, "West");
        }

        void setRoof(const TileDefinition::TileData *tile)
        {
            if (tiles[SectionRoof] == nullptr)
            {
                tiles[SectionRoof] = tile;
            }
            else
            {
                tiles[SectionRoof2] = tile;
            }
        }

        void setRoofCap(const TileDefinition::TileData *tile)
        {
            if (tiles[SectionRoofCap] != nullptr)
            {
                tiles[SectionRoofCap2] = tile;
            }
            else
            {
                tiles[SectionRoofCap] = tile;
            }
        }
    };

    struct BuildingFloor : public Loggable<BuildingFloor>
    {
        struct FloorTileGrid
        {
            std::unordered_map<uint32_t, const TileDefinition::TileData *> tiles;

            const TileDefinition::TileData *at(uint32_t index) const
            {
                auto it = tiles.find(index);
                if (it == tiles.end())
                {
                    return nullptr;
                }

                return it->second;
            }
        };

        Building *building = nullptr;

        std::vector<Square> squares;
        std::vector<int32_t> roomIndices;
        std::vector<std::unique_ptr<BuildingObject>> objects;
        std::unordered_map<std::string, FloorTileGrid> userTiles;

        std::int32_t layer;
        std::int32_t width;
        std::int32_t height;

        // move constructors
        BuildingFloor(BuildingFloor &&) noexcept = default;
        BuildingFloor &operator=(BuildingFloor &&) noexcept = default;

        // delete the copy constructors to make the class move-only (for storing BuildingObject ptrs)
        BuildingFloor(const BuildingFloor &) = delete;
        BuildingFloor &operator=(const BuildingFloor &) = delete;

        BuildingFloor(Building *building, int32_t layer) noexcept :
                building(building),
                layer(layer),
                width(building->width + 1),
                height(building->height + 1)
        {
            squares.resize(width * height);
        }

        void readDoorObject(const pugi::xml_node &objectNode)
        {
            auto door = std::make_unique<DoorObject>();

            door->x = objectNode.attribute("x").as_int();
            door->y = objectNode.attribute("y").as_int();
            door->tile = objectNode.attribute("Tile").as_int();
            door->frameTile = objectNode.attribute("FrameTile").as_int();
            door->direction = getDirection(objectNode.attribute("dir").as_string());

            objects.push_back(std::move(door));
        }

        void readStairsObject(const pugi::xml_node &objectNode)
        {
            auto stairs = std::make_unique<StairsObject>();

            stairs->tile = objectNode.attribute("Tile").as_int();
            stairs->x = objectNode.attribute("x").as_int();
            stairs->y = objectNode.attribute("y").as_int();
            stairs->direction = getDirection(objectNode.attribute("dir").as_string());

            objects.push_back(std::move(stairs));
        }

        void readWindowObject(const pugi::xml_node &objectNode)
        {
            auto window = std::make_unique<WindowObject>();

            window->x = objectNode.attribute("x").as_int();
            window->y = objectNode.attribute("y").as_int();
            window->direction = getDirection(objectNode.attribute("dir").as_string());

            window->tile = objectNode.attribute("Tile").as_int();
            window->curtainsTile = objectNode.attribute("CurtainsTile").as_int();
            window->shuttersTile = objectNode.attribute("ShuttersTile").as_int();

            objects.push_back(std::move(window));
        }

        void readFurnitureObject(const pugi::xml_node &objectNode)
        {
            auto furniture = std::make_unique<FurnitureObject>();

            furniture->x = objectNode.attribute("x").as_int();
            furniture->y = objectNode.attribute("y").as_int();
            furniture->furnitureTiles = objectNode.attribute("FurnitureTiles").as_int();
            furniture->direction = getDirection(objectNode.attribute("orient").as_string());

            objects.push_back(std::move(furniture));
        }

        void readRoofObject(const pugi::xml_node &objectNode)
        {
            objects.push_back(std::make_unique<RoofObject>(objectNode));
        }

        void readWallObject(const pugi::xml_node &objectNode)
        {
            auto wall = std::make_unique<WallObject>();

            wall->direction = getDirection(objectNode.attribute("dir").as_string());

            wall->length = objectNode.attribute("length").as_int();
            wall->x = objectNode.attribute("x").as_int();
            wall->y = objectNode.attribute("y").as_int();

            wall->tile = objectNode.attribute("Tile").as_int();
            wall->interiorTile = objectNode.attribute("InteriorTile").as_int();
            wall->interiorTrim = objectNode.attribute("InteriorTrim").as_int();
            wall->exteriorTrim = objectNode.attribute("ExteriorTrim").as_int();

            objects.push_back(std::move(wall));
        }

        void readObject(const pugi::xml_node &objectNode)
        {
            const std::string type = objectNode.attribute("type").as_string();

            if (type == "door")
                readDoorObject(objectNode);

            else if (type == "stairs")
                readStairsObject(objectNode);

            else if (type == "window")
                readWindowObject(objectNode);

            else if (type == "furniture")
                readFurnitureObject(objectNode);

            else if (type == "roof")
                readRoofObject(objectNode);

            else if (type == "wall")
                readWallObject(objectNode);

            else
                throw std::runtime_error("Invalid object type: " + type);
        }

        void readRoom(const pugi::xml_node &roomNode)
        {
            roomIndices.assign(width * height, 0);

            const std::string csvGrid = roomNode.text().as_string();
            const char *ptr = csvGrid.data();
            const char *end = ptr + csvGrid.size();

            int sourceWidth = width - 1;
            int count = 0;

            while (ptr < end)
            {
                int value;
                auto [next_ptr, ec] = std::from_chars(ptr, end, value);

                if (ec == std::errc())
                {
                    int x = count % sourceWidth;
                    int y = count / sourceWidth;

                    roomIndices[y * width + x] = value;

                    ptr = next_ptr;
                    count++;
                }
                else
                {
                    ptr++;
                }
            }
        }

        void readTiles(const pugi::xml_node &tilesNode)
        {
            const std::string layerName = tilesNode.attribute("layer").as_string();
            const std::string csvGrid = tilesNode.text().as_string();
            const char *ptr = csvGrid.data();
            const char *end = ptr + csvGrid.size();

            int count = 0;

            while (ptr < end)
            {
                int value;
                auto [next_ptr, ec] = std::from_chars(ptr, end, value);

                if (ec == std::errc())
                {
                    int x = count % width;
                    int y = count / width;

                    const TileDefinition::TileData *userTile = building->getUserTile(value);

                    if (userTile != nullptr)
                    {
                        userTiles[layerName].tiles[x + y * width] = userTile;
                    }

                    ptr = next_ptr;
                    count++;
                }
                else
                {
                    ptr++;
                }
            }
        }

        bool isInBounds(int x, int y) const
        {
            return x >= 0 && x < width && y >= 0 && y < height;
        }

        Square &square(int x, int y)
        {
            return squares[x + y * width];
        }

        const BuildingRoom *getRoom(int x, int y)
        {
            int index = x + y * width;

            if (index < 0 || index >= roomIndices.size())
                return nullptr;

            return building->getRoom(roomIndices[index]);
        }

        int getRoomIndex(int x, int y)
        {
            int index = x + y * width;

            if (index < 0 || index >= roomIndices.size())
                return 0;

            return roomIndices[index];
        }

        void setWalls()
        {
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    Square &square = this->square(x, y);

                    const BuildingRoom *room = getRoom(x, y);
                    const BuildingRoom *roomN = getRoom(x, y - 1);
                    const BuildingRoom *roomW = getRoom(x - 1, y);

                    if (room && !room->emptyOutside())
                    {
                        const BuildingTileEntry *interiorWall = building->getTileEntry(room->interiorWall);
                        const BuildingTileEntry *interiorWallTrim = building->getTileEntry(room->interiorWallTrim);
                        const BuildingTileEntry *interiorWallGrime = building->getTileEntry(room->grimeWall);

                        if (roomN != room) square.setWallN(interiorWall, interiorWallTrim, interiorWallGrime);
                        if (roomW != room) square.setWallW(interiorWall, interiorWallTrim, interiorWallGrime);
                    }
                    else
                    {
                        bool hasWallN = roomN && !roomN->emptyOutside();
                        bool hasWallW = roomW && !roomW->emptyOutside();

                        const BuildingTileEntry *wall = building->exteriorWall;
                        const BuildingTileEntry *wallTrim = building->exteriorWallTrim;
                        const BuildingTileEntry *wallGrime = building->grimeWall;

                        if (layer != 0)
                        {
                            wallTrim = nullptr;
                            wallGrime = nullptr;
                        }

                        if (hasWallN) square.setWallN(wall, wallTrim, wallGrime);
                        if (hasWallW) square.setWallW(wall, wallTrim, wallGrime);
                    }
                }
            }
        }

        void setWallObjects()
        {
            for (auto &object : objects)
            {
                WallObject *wall = object->asWall();

                if (wall == nullptr)
                    continue;

                const BuildingTileEntry *interiorTile = building->getTileEntry(wall->interiorTile);
                const BuildingTileEntry *interiorTrim = building->getTileEntry(wall->interiorTrim);

                const BuildingTileEntry *exteriorTile = building->getTileEntry(wall->tile);
                const BuildingTileEntry *exteriorTrim = building->getTileEntry(wall->exteriorTrim);

                if (wall->isNorth())
                {
                    int x = wall->x;

                    for (int y = wall->y; y < wall->y + wall->length; y++)
                    {
                        const BuildingRoom *room = getRoom(x, y);

                        if (room && !room->emptyOutside())
                        {
                            square(x, y).setWallW(interiorTile, interiorTrim, building->getTileEntry(room->grimeWall));
                        }
                        else if (layer == 0)
                        {
                            square(x, y).setWallW(exteriorTile, exteriorTrim, building->grimeWall);
                        }
                        else
                        {
                            square(x, y).setWallW(exteriorTile, exteriorTrim, nullptr);
                        }
                    }
                }

                if (wall->isWest())
                {
                    int y = wall->y;

                    for (int x = wall->x; x < wall->x + wall->length; x++)
                    {
                        const BuildingRoom *room = getRoom(x, y);

                        if (room && !room->emptyOutside())
                        {
                            square(x, y).setWallN(interiorTile, interiorTrim, building->getTileEntry(room->grimeWall));
                        }
                        else if (layer == 0)
                        {
                            square(x, y).setWallN(exteriorTile, exteriorTrim, building->grimeWall);
                        }
                        else
                        {
                            square(x, y).setWallN(exteriorTile, exteriorTrim, nullptr);
                        }
                    }
                }
            }
        }

        void setWallFurnitures()
        {
            for (auto &object : objects)
            {
                const FurnitureObject *furnitureObj = object->asFurniture();

                if (furnitureObj == nullptr)
                    continue;

                const FurnitureTiles *furnitureTiles = building->getFurniture(furnitureObj->furnitureTiles);

                if (furnitureTiles->layer != FurnitureTiles::LayerWalls)
                    continue;

                const bool killN = furnitureObj->direction == Direction::N || furnitureObj->direction == Direction::S;
                const bool killW = furnitureObj->direction == Direction::W || furnitureObj->direction == Direction::E;

                int furnitureX = furnitureObj->x;
                int furnitureY = furnitureObj->y;

                if (furnitureObj->direction == Direction::E) furnitureX++;
                if (furnitureObj->direction == Direction::S) furnitureY++;

                const FurnitureTile *fTile = furnitureTiles->tryGet(furnitureObj->direction);

                if (fTile == nullptr)
                {
                    logger.warning("setWallFurnitures: {},{}, invalid direction: '{}'",
                        furnitureObj->x,
                        furnitureObj->y,
                        magic_enum::enum_name(furnitureObj->direction));

                    for (const auto &[dir, tilename] : furnitureTiles->tiles)
                    {
                        logger.warning("\t * direction: {}", magic_enum::enum_name(dir));
                    }

                    continue;
                }

                for (const auto &tile : fTile->tiles)
                {
                    int x = furnitureX + tile.x;
                    int y = furnitureY + tile.y;

                    Square &square = this->square(x, y);

                    if (killN)
                    {
                        square.wallN.allowGrime = fTile->grime;
                        square.wallN.furnitureTile = &tile;
                    }

                    if (killW)
                    {
                        square.wallW.allowGrime = fTile->grime;
                        square.wallW.furnitureTile = &tile;
                    }
                }
            }
        }

        void setWindowObjects()
        {
            for (auto &object : objects)
            {
                WindowObject *window = object->asWindow();

                if (window == nullptr)
                    continue;

                const BuildingTileEntry *windowTileEntry = building->getTileEntry(window->tile);
                const BuildingTileEntry *curtainsTileEntry = building->getTileEntry(window->curtainsTile);
                const BuildingTileEntry *shuttersTileEntry = building->getTileEntry(window->shuttersTile);

                const int x = window->x;
                const int y = window->y;

                const BuildingRoom *room = getRoom(x, y);
                const BuildingRoom *roomN = getRoom(x, y - 1);
                const BuildingRoom *roomW = getRoom(x - 1, y);

                if (window->isNorth())
                {
                    square(x, y).setWindowN(windowTileEntry);

                    if (room && !room->emptyOutside())
                    {
                        square(x, y).setTile(Square::SectionCurtains, curtainsTileEntry, "North");
                    }
                    else if (roomN)
                    {
                        square(x, y - 1).setTile(Square::SectionCurtains, curtainsTileEntry, "South");

                        square(x, y).setTile(Square::SectionWallFurniture, shuttersTileEntry, "NorthLeft");
                        square(x, y).setTile(Square::SectionWallFurniture2, shuttersTileEntry, "NorthRight");
                        square(x + 1, y).setTile(Square::SectionWallFurniture, shuttersTileEntry, "NorthRight");
                        square(x - 1, y).setTile(Square::SectionWallFurniture, shuttersTileEntry, "NorthLeft");
                    }
                }

                if (window->isWest())
                {
                    square(x, y).setWindowW(windowTileEntry);

                    if (room && !room->emptyOutside())
                    {
                        square(x, y).setTile(Square::SectionCurtains, curtainsTileEntry, "West");
                    }
                    else if (roomW)
                    {
                        square(x - 1, y).setTile(Square::SectionCurtains, curtainsTileEntry, "East");

                        square(x, y).setTile(Square::SectionWallFurniture, shuttersTileEntry, "WestAbove");
                        square(x, y).setTile(Square::SectionWallFurniture2, shuttersTileEntry, "WestBelow");
                        square(x, y - 1).setTile(Square::SectionWallFurniture, shuttersTileEntry, "WestAbove");
                        square(x, y + 1).setTile(Square::SectionWallFurniture, shuttersTileEntry, "WestBelow");
                    }
                }
            }
        }

        void setDoorObjects()
        {
            for (auto &object : objects)
            {
                DoorObject *door = object->asDoor();

                if (door == nullptr)
                    continue;

                Square &square = this->square(door->x, door->y);

                const BuildingTileEntry *doorTileEntry = building->getTileEntry(door->tile);
                const BuildingTileEntry *frameTileEntry = building->getTileEntry(door->frameTile);

                if (door->isNorth()) square.setDoorN(doorTileEntry, frameTileEntry);
                if (door->isWest()) square.setDoorW(doorTileEntry, frameTileEntry);
            }
        }

        void removeFloor(int x, int y)
        {
            square(x, y).allowFloor = false;
        }

        void setStairObjects()
        {
            for (auto &object : objects)
            {
                StairsObject *stairs = object->asStairs();

                if (stairs == nullptr)
                    continue;

                const BuildingTileEntry *stairsEntry = building->getTileEntry(stairs->tile);

                const int x = stairs->x;
                const int y = stairs->y;

                const Square::Section sectionMin = Square::SectionFurniture;
                const Square::Section sectionMax = Square::SectionFurniture4;

                BuildingFloor *upperFloor = building->getFloor(layer + 1);

                if (stairs->isNorth())
                {
                    square(x, y + 1).setTile(stairsEntry, "North3", sectionMin, sectionMax);
                    square(x, y + 2).setTile(stairsEntry, "North2", sectionMin, sectionMax);
                    square(x, y + 3).setTile(stairsEntry, "North1", sectionMin, sectionMax);

                    if (upperFloor != nullptr)
                    {
                        upperFloor->removeFloor(x, y + 1);
                        upperFloor->removeFloor(x, y + 2);
                        upperFloor->removeFloor(x, y + 3);
                    }
                }

                if (stairs->isWest())
                {
                    square(x + 1, y).setTile(stairsEntry, "West3", sectionMin, sectionMax);
                    square(x + 2, y).setTile(stairsEntry, "West2", sectionMin, sectionMax);
                    square(x + 3, y).setTile(stairsEntry, "West1", sectionMin, sectionMax);

                    if (upperFloor != nullptr)
                    {
                        upperFloor->removeFloor(x + 1, y);
                        upperFloor->removeFloor(x + 2, y);
                        upperFloor->removeFloor(x + 3, y);
                    }
                }
            }
        }

        void setLayerRoofCap(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            if (furnitureObject->direction == Direction::E) x++;
            if (furnitureObject->direction == Direction::S) y++;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                square(x + tile.x, y + tile.y).setTile(tile.tile, Square::SectionRoofCap, Square::SectionRoofCap2);
            }
        }

        void setLayerWallOverlay(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                Square &square = this->square(x + tile.x, y + tile.y);

                if (furnitureObject->isWest() || furnitureObject->isNorth())
                {
                    square.setTile(tile.tile, Square::SectionWallOverlay, Square::SectionWallOverlay2);
                }
                else
                {
                    square.setTile(tile.tile, Square::SectionWallOverlay3, Square::SectionWallOverlay4);
                }
            }
        }

        void setLayerWallFurniture(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                Square &square = this->square(x + tile.x, y + tile.y);

                if (furnitureObject->isWest() || furnitureObject->isNorth())
                {
                    square.setTile(tile.tile, Square::SectionWallFurniture, Square::SectionWallFurniture2);
                }
                else
                {
                    square.setTile(tile.tile, Square::SectionWallFurniture3, Square::SectionWallFurniture4);
                }
            }
        }

        void setLayerFurniture(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                square(x + tile.x, y + tile.y).setTile(tile.tile, Square::SectionFurniture, Square::SectionFurniture4);
            }
        }

        void setLayerFrames(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            if (furnitureObject->direction == Direction::E) x++;
            if (furnitureObject->direction == Direction::S) y++;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                square(x + tile.x, y + tile.y).setTile(tile.tile, Square::SectionFrame, Square::SectionFrame);
            }
        }

        void setLayerDoors(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            logger.warning("setLayerDoors not implemented");
        }

        void setLayerRoof(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            int x = furnitureObject->x;
            int y = furnitureObject->y;

            const FurnitureTile *furnitureTile = furnitureTiles->tryGet(furnitureObject->direction);

            if (furnitureTile == nullptr)
                return;

            for (const FurnitureTile::Tile &tile : furnitureTile->tiles)
            {
                square(x + tile.x, y + tile.y).setTile(tile.tile, Square::SectionRoof, Square::SectionRoof2);
            }
        }

        void setLayerFloorFurniture(const FurnitureObject *furnitureObject, const FurnitureTiles *furnitureTiles)
        {
            logger.warning("setLayerFloorFurniture not implemented");
        }

        void setFurnitureObjects()
        {
            for (auto &object : objects)
            {
                const FurnitureObject *furnitureObj = object->asFurniture();

                if (furnitureObj == nullptr)
                    continue;

                const FurnitureTiles *furnitureTiles = building->getFurniture(furnitureObj->furnitureTiles);

                switch (furnitureTiles->layer)
                {
                    case FurnitureTiles::LayerWalls:
                        // already done in setWallFurnitures
                        break;

                    case FurnitureTiles::LayerRoofCap:
                        setLayerRoofCap(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerWallOverlay:
                        setLayerWallOverlay(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerWallFurniture:
                        setLayerWallFurniture(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerFurniture:
                        setLayerFurniture(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerFrames:
                        setLayerFrames(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerDoors:
                        setLayerDoors(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerRoof:
                        setLayerRoof(furnitureObj, furnitureTiles);
                        break;

                    case FurnitureTiles::LayerFloorFurniture:
                        setLayerFloorFurniture(furnitureObj, furnitureTiles);
                        break;

                    default:
                        throw std::runtime_error(std::format("Invalid furniture layer: '{}'", static_cast<uint8_t>(furnitureTiles->layer)));
                }
            }
        }

        void setRoofSlopes(const RoofObject *roofObject)
        {
            Rectangle2D rect;

            const BuildingTileEntry *tileEntry = building->getTileEntry(roofObject->slopeTiles);
            const std::vector<RoofObject::RoofTile> roofTiles = roofObject->getSlopeTiles(rect);

            if (roofTiles.empty())
                return;

            // logger.info("setRoofSlopes: x=\"{}\" y=\"{}\", rect: [{}, {}, {}, {}], roofType: {}, roofDepth: {}",
            //     roofObject->x,
            //     roofObject->y,
            //     rect.x,
            //     rect.y,
            //     rect.width,
            //     rect.height,
            //     magic_enum::enum_name(roofObject->roofType),
            //     magic_enum::enum_name(roofObject->roofDepth));

            for (int x = 0; x < rect.width; x++)
            {
                for (int y = 0; y < rect.height; y++)
                {
                    const RoofObject::RoofTile roofTile = roofTiles[x + y * rect.width];
                    const std::string roofTileName = RoofObject::tileToName(roofTile);
                    const BuildingTile &tile = tileEntry->tiles.at(roofTileName);

                    int tx = x + rect.x + tile.tileOffset.x;
                    int ty = y + rect.y + tile.tileOffset.y;

                    if (isInBounds(tx, ty))
                    {
                        square(tx, ty).setRoof(tile.tileData);
                    }
                }
            }
        }

        void setRoofCaps(const Rectangle2D &rect, const BuildingTileEntry *tileEntry, const std::vector<RoofObject::RoofTile> &&roofTiles)
        {
            if (roofTiles.empty())
                return;

            for (int x = 0; x < rect.width; x++)
            {
                for (int y = 0; y < rect.height; y++)
                {
                    if (x + y * rect.width >= roofTiles.size())
                        continue;

                    const RoofObject::RoofTile roofTile = roofTiles[x + y * rect.width];
                    const std::string roofTileName = RoofObject::tileToName(roofTile);
                    const BuildingTile &tile = tileEntry->tiles.at(roofTileName);

                    int tx = x + rect.x + tile.tileOffset.x;
                    int ty = y + rect.y + tile.tileOffset.y;

                    if (isInBounds(tx, ty))
                    {
                        square(tx, ty).setRoofCap(tile.tileData);
                    }
                }
            }
        }

        void setRoofCaps(const RoofObject *roofObject)
        {
            Rectangle2D rect;

            const auto *tileEntry = building->getTileEntry(roofObject->capTiles);

            setRoofCaps(rect, tileEntry, roofObject->getWestCapTiles(rect));
            setRoofCaps(rect, tileEntry, roofObject->getEastCapTiles(rect));
            setRoofCaps(rect, tileEntry, roofObject->getNorthCapTiles(rect));
            setRoofCaps(rect, tileEntry, roofObject->getSouthCapTiles(rect));
        }

        void setRoofCorners(const RoofObject *roofObject)
        {
            Rectangle2D rect;

            const BuildingTileEntry *tileEntry = building->getTileEntry(roofObject->slopeTiles);
            const std::vector<RoofObject::RoofTile> roofTiles = roofObject->getCornerTiles(rect);

            if (roofTiles.empty())
                return;

            // logger.info("setRoofCorners: x=\"{}\" y=\"{}\", rect: [{}, {}, {}, {}], roofType: {}, roofDepth: {}",
            //     roofObject->x,
            //     roofObject->y,
            //     rect.x,
            //     rect.y,
            //     rect.width,
            //     rect.height,
            //     magic_enum::enum_name(roofObject->roofType),
            //     magic_enum::enum_name(roofObject->roofDepth));

            for (int x = 0; x < rect.width; x++)
            {
                for (int y = 0; y < rect.height; y++)
                {
                    const RoofObject::RoofTile roofTile = roofTiles[x + y * rect.width];

                    if (roofTile != RoofObject::TileCount)
                    {
                        const std::string roofTileName = RoofObject::tileToName(roofTile);
                        const BuildingTile &tile = tileEntry->tiles.at(roofTileName);

                        int tx = x + rect.x + tile.tileOffset.x;
                        int ty = y + rect.y + tile.tileOffset.y;

                        if (isInBounds(tx, ty))
                        {
                            square(tx, ty).setRoof(tile.tileData);
                        }
                    }
                }
            }
        }

        void setRoofTops(const RoofObject *roofObject)
        {
            if (roofObject->roofDepth == RoofObject::Three)
            {
                // mFlatRoofsWithDepthThree += ro;
                return;
            }

            Rectangle2D rect = roofObject->flatTop();

            const BuildingTileEntry *tileEntry = building->getTileEntry(roofObject->topTiles);

            if (tileEntry == nullptr)
                return;

            // logger.info("setRoofTops: x=\"{}\" y=\"{}\", rect: [{}, {}, {}, {}], roofType: {}, roofDepth: {}, tileEntry: {}",
            //     roofObject->x,
            //     roofObject->y,
            //     rect.x,
            //     rect.y,
            //     rect.width,
            //     rect.height,
            //     magic_enum::enum_name(roofObject->roofType),
            //     magic_enum::enum_name(roofObject->roofDepth),
            //     tileEntry->category);

            std::string _enum;

            switch (roofObject->roofDepth)
            {
                case RoofObject::One:
                    _enum = roofObject->isNorth() ? "North1" : "West1";
                    break;

                case RoofObject::Two:
                    _enum = roofObject->isNorth() ? "North2" : "West2";
                    break;

                case RoofObject::Three:
                case RoofObject::Zero:
                    _enum = roofObject->isNorth() ? "North3" : "West3";
                    break;

                default:
                    logger.warning("Invalid roofTop depth: {}", magic_enum::enum_name(roofObject->roofDepth));
                    return;
            }

            Vector2i offset = tileEntry->tiles.at(_enum).tileOffset;

            rect.x += offset.x;
            rect.y += offset.y;

            for (int x = 0; x < rect.width; x++)
            {
                for (int y = 0; y < rect.height; y++)
                {
                    if (!isInBounds(x, y))
                        continue;

                    if (roofObject->roofDepth == RoofObject::Zero || roofObject->roofDepth == RoofObject::Three)
                    {
                        square(x + rect.x, y + rect.y).setFloor(tileEntry, _enum);
                    }
                    else
                    {
                        square(x + rect.x, y + rect.y).setTile(Building::Square::SectionRoofTop, tileEntry, _enum);
                    }
                }
            }
        }

        void setRoof()
        {
            for (auto &object : objects)
            {
                const RoofObject *roofObject = object->asRoof();

                if (roofObject == nullptr)
                    continue;

                setRoofSlopes(roofObject);
                setRoofCaps(roofObject);
                setRoofCorners(roofObject);
                setRoofTops(roofObject);
            }
        }

        void setUserWalls(const std::string &layerName)
        {
            if (!userTiles.contains(layerName))
                return;

            const FloorTileGrid *gridWalls = &userTiles.at(layerName);

            for (int x = 0; x < width; x++)
            {
                for (int y = 0; y < height; y++)
                {
                    const uint32_t index = x + y * width;
                    const BuildingRoom *room = getRoom(x, y);
                    const TileDefinition::TileData *tileData = gridWalls ? gridWalls->at(index) : nullptr;

                    if (tileData == nullptr)
                        continue;

                    auto hasProperty = [&tileData](const std::vector<std::string> &keys) -> bool
                    {
                        for (const auto &key : keys)
                        {
                            if (tileData->properties.contains(key))
                            {
                                return true;
                            }
                        }

                        return false;
                    };

                    bool wallN = hasProperty({ "DoorWallN", "WallN", "WallNTrans", "windowN", "windowN", "WallNW" });
                    bool wallW = hasProperty({ "DoorWallW", "WallW", "WallWTrans", "windowW", "WindowW", "WallNW" });
                    bool wallOverlay = hasProperty({ "WallOverlay" });
                    bool wallNW = hasProperty({ "WallNW" });

                    if (wallOverlay)
                    {
                        square(x, y).setTile(tileData, Square::SectionWallOverlay, Square::SectionWallOverlay4);
                    }
                    else if (wallNW)
                    {
                        square(x, y).wallN.userTile = tileData;
                        square(x, y).wallW.userTile = tileData;
                    }
                    else if (wallN)
                    {
                        square(x, y).wallN.userTile = tileData;
                    }
                    else if (wallW)
                    {
                        square(x, y).wallW.userTile = tileData;
                    }
                    else // if (hasProperty({ "WallSE" }))
                    {
                        square(x, y).setTile(tileData, Square::SectionWallOverlay, Square::SectionWallOverlay4);
                    }
                }
            }
        }

        void setUserTiles()
        {
            // clang-format off
            static const std::unordered_map<std::string_view, Square::Section> layerToSection = {
                { "Curtains",       { Square::SectionCurtains } },
                { "Curtains2",      { Square::SectionCurtains2 } },
                { "Doors",          { Square::SectionDoor } },
                { "Floor",          { Square::SectionFloor } },
                { "FloorGrime",     { Square::SectionFloorGrime } },
                { "FloorGrime2",    { Square::SectionFloorGrime2 } },
                { "FloorOverlay",   { Square::SectionFloorFurniture } },
                { "Frames",         { Square::SectionFrame } },
                { "Furniture",      { Square::SectionFurniture } },
                { "Furniture2",     { Square::SectionFurniture2 } },
                { "Furniture3",     { Square::SectionFurniture3 } },
                { "Furniture4",     { Square::SectionFurniture4 } },
                { "Roof",           { Square::SectionRoof } },
                { "Roof2",          { Square::SectionRoof2 } },
                { "RoofCap",        { Square::SectionRoofCap } },
                { "RoofCap2",       { Square::SectionRoofCap2 } },
                { "RoofTop",        { Square::SectionRoofTop } },
                { "Vegetation",     { Square::SectionFloorFurniture } },
                { "WallFurniture",  { Square::SectionWallFurniture } },
                { "WallFurniture2", { Square::SectionWallFurniture2 } },
                { "WallFurniture3", { Square::SectionWallFurniture3 } },
                { "WallFurniture4", { Square::SectionWallFurniture4 } },
                { "WallOverlay",    { Square::SectionWallOverlay } },
                { "WallOverlay2",   { Square::SectionWallOverlay2 } },
                { "WallOverlay3",   { Square::SectionWallOverlay3 } },
                { "WallOverlay4",   { Square::SectionWallOverlay4 } },
                { "Walls",          { Square::SectionWall } },
                { "Walls2",         { Square::SectionWall2 } },
                { "WallGrime",      { Square::SectionWallGrime } },
                { "WallGrime2",     { Square::SectionWallGrime2 } },
                { "WallTrim",       { Square::SectionWallTrim } },
                { "WallTrim2",      { Square::SectionWallTrim2 } },
                { "Windows",        { Square::SectionWindow } },
            }; // clang-format on

            for (const auto &[layerName, tilesGrid] : userTiles)
            {
                if (layerName == "Walls" || layerName == "Walls2")
                    continue;

                if (!layerToSection.contains(layerName))
                {
                    logger.warning("UserTiles: layer not found '{}'", layerName);
                    continue;
                }

                auto section = layerToSection.at(layerName);

                for (int x = 0; x < width; x++)
                {
                    for (int y = 0; y < height; y++)
                    {
                        auto tile = tilesGrid.at(x + y * width);

                        if (tile != nullptr)
                        {
                            square(x, y).tiles[section] = tile;
                        }
                    }
                }
            }
        }

        void setWallCorners()
        {
            for (int y = 1; y < height; y++)
            {
                for (int x = 1; x < width; x++)
                {
                    Square &square = this->square(x, y);

                    bool cornerW = !square.hasWallW() && this->square(x, y - 1).hasWallW();
                    bool cornerN = !square.hasWallN() && this->square(x - 1, y).hasWallN();

                    if (!cornerN || !cornerW)
                        continue;

                    const BuildingRoom *room = getRoom(x, y);
                    const bool isInteriorWall = room && !room->emptyOutside();

                    const BuildingTileEntry *wallEntry = isInteriorWall
                        ? building->getTileEntry(room->interiorWall)
                        : building->exteriorWall;

                    const BuildingTileEntry *trimEntry = isInteriorWall
                        ? building->getTileEntry(room->interiorWallTrim)
                        : building->exteriorWallTrim;

                    const BuildingTileEntry *grimEntry = isInteriorWall
                        ? building->getTileEntry(room->grimeWall)
                        : building->grimeWall;

                    if (wallEntry)
                        square.setTile(wallEntry->tiles.at("SouthEast").tileData, Square::SectionWall, Square::SectionWall2);

                    if (trimEntry)
                        square.setTile(trimEntry->tiles.at("SouthEast").tileData, Square::SectionWallTrim, Square::SectionWallTrim2);

                    if (grimEntry && (layer == 0 || isInteriorWall))
                        square.setTile(grimEntry->tiles.at("SouthEast").tileData, Square::SectionWallGrime, Square::SectionWallGrime2);
                }
            }
        }

        void replaceWalls()
        {
            setUserWalls("Walls");
            setUserWalls("Walls2");

            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    Square &square = this->square(x, y);

                    const BuildingTileEntry *wallN = square.wallN.wall;
                    const BuildingTileEntry *wallW = square.wallW.wall;

                    const bool isWallNW = wallN && wallW && wallN == wallW;
                    const bool hasOpening = square.hasDoor() || square.hasWindow();

                    if (isWallNW && !hasOpening)
                    {
                        square.setTile(Square::sectionWallN, square.wallN.wall, square.wallN._enumNW());
                        square.setTile(Square::sectionWallTrimN, square.wallN.trim, square.wallN._enumNW());
                        square.setTile(Square::sectionWallGrimeN, square.wallN.grime, square.wallN._enumNW());
                    }
                    else
                    {
                        if (wallN)
                        {
                            square.setTile(Square::sectionWallN, square.wallN.wall, square.wallN._enum);
                            square.setTile(Square::sectionWallTrimN, square.wallN.trim, square.wallN._enum);
                            square.setTile(Square::sectionWallGrimeN, square.wallN.grime, square.wallN._enum);
                        }

                        if (wallW)
                        {
                            square.setTile(Square::sectionWallW, square.wallW.wall, square.wallW._enum);
                            square.setTile(Square::sectionWallTrimW, square.wallW.trim, square.wallW._enum);
                            square.setTile(Square::sectionWallGrimeW, square.wallW.grime, square.wallW._enum);
                        }
                    }

                    // wall furniture tiles replace all tiles set previously
                    if (square.wallN.furnitureTile)
                    {
                        square.tiles[Square::sectionWallN] = square.wallN.furnitureTile->tile;
                        square.tiles[Square::sectionWallTrimN] = nullptr;
                    }

                    if (square.wallW.furnitureTile)
                    {
                        square.tiles[Square::sectionWallW] = square.wallW.furnitureTile->tile;
                        square.tiles[Square::sectionWallTrimW] = nullptr;
                    }

                    // user tiles replace all tiles set previously
                    if (square.wallN.userTile)
                    {
                        square.tiles[Square::sectionWallN] = square.wallN.userTile;
                    }

                    if (square.wallW.userTile)
                    {
                        square.tiles[Square::sectionWallW] = square.wallW.userTile;
                    }

                    const BuildingRoom *room = getRoom(x, y);

                    if (room)
                    {
                        square.setFloor(building->getTileEntry(room->floor));
                        square.setFloorGrime(building->getTileEntry(room->grimeFloor));
                    }
                }
            }
        }

        const std::vector<Square> &toSquares()
        {
            setWalls();
            setWallObjects();
            setWallFurnitures();

            setDoorObjects();
            setWindowObjects();
            setStairObjects();
            setFurnitureObjects();
            setRoof();

            replaceWalls();
            setWallCorners();
            setUserTiles();

            return squares;
        }

        const std::unordered_map<int32_t, std::vector<Rectangle2D>> getRoomRectangles() const
        {
            std::unordered_map<int32_t, std::vector<Rectangle2D>> rectangles;
            std::vector<int32_t> roomIndices = this->roomIndices;

            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    uint32_t index = x + y * width;
                    int32_t roomIndex = roomIndices[index];

                    if (roomIndex == 0)
                        continue;

                    int w = 0;
                    while ((x + w) < width && roomIndices[(x + w) + y * width] == roomIndex)
                    {
                        w++;
                    }

                    int h = 1;
                    bool canExpandDown = true;

                    while ((y + h) < height && canExpandDown)
                    {
                        for (int k = 0; k < w; k++)
                        {
                            if (roomIndices[(y + h) * width + (x + k)] != roomIndex)
                            {
                                canExpandDown = false;
                                break;
                            }
                        }
                        if (canExpandDown)
                        {
                            h++;
                        }
                    }

                    rectangles[roomIndex].push_back({ x, y, w, h });

                    for (int dy = 0; dy < h; dy++)
                    {
                        for (int dx = 0; dx < w; dx++)
                        {
                            roomIndices[(y + dy) * width + (x + dx)] = 0;
                        }
                    }
                }
            }

            return rectangles;
        }
    };

    pugi::xml_document doc;

    std::unordered_map<std::string, std::string> properties;

    int32_t version = -1;
    int32_t width = 0;
    int32_t height = 0;

    int32_t minLayer = 0;
    int32_t maxLayer = INT32_MIN;

    const BuildingTileEntry *exteriorWall;
    const BuildingTileEntry *exteriorWallTrim;
    const BuildingTileEntry *door;
    const BuildingTileEntry *doorFrame;
    const BuildingTileEntry *window;
    const BuildingTileEntry *curtains;
    const BuildingTileEntry *shutters;
    const BuildingTileEntry *stairs;
    const BuildingTileEntry *roofCap;
    const BuildingTileEntry *roofSlope;
    const BuildingTileEntry *roofTop;
    const BuildingTileEntry *grimeWall;

    std::vector<BuildingTileEntry> tileEntries;
    std::vector<FurnitureTiles> furnitures;
    std::vector<BuildingFloor> floors;
    std::vector<BuildingRoom> rooms;
    std::vector<const TileDefinition::TileData *> userTiles;

    const TilesManager *tilesManager;

public:
    std::string path;

    Building(const TilesManager *tilesManager) : tilesManager(tilesManager) {}
    Building(const std::string &path, const TilesManager *tilesManager) : tilesManager(tilesManager)
    {
        this->path = path;
        read(path);
    }

    static Direction switchDirection(Direction dir)
    {
        // clang-format off
        switch (dir)
        {
            case Direction::N:  return Direction::S;
            case Direction::W:  return Direction::E;
            case Direction::E:  return Direction::W;
            case Direction::S:  return Direction::N;
            case Direction::NW: return Direction::SE;
            case Direction::NE: return Direction::SW;
            case Direction::SW: return Direction::NE;
            case Direction::SE: return Direction::NW;

            default: return Direction::Invalid;
        } // clang-format on
    }

    static Direction getDirection(const std::string &strDirection)
    {
        static const std::unordered_map<std::string, Direction> lookup = {
            { "N", Direction::N },
            { "S", Direction::S },
            { "W", Direction::W },
            { "E", Direction::E },
            { "NW", Direction::NW },
            { "NE", Direction::NE },
            { "SW", Direction::SW },
            { "SE", Direction::SE },
        };

        auto it = lookup.find(strDirection);
        if (it != lookup.end())
        {
            return it->second;
        }

        logger.warning("invalid direction: '{}'", strDirection);

        return Direction::Invalid;
    }

    const BuildingTileEntry *getTileEntry(uint32_t index) const
    {
        if (index == 0)
        {
            return nullptr;
        }

        return &tileEntries[index - 1];
    }

    const BuildingTileEntry *getTileEntry(const pugi::xml_node &buildingNode, const std::string &name) const
    {
        auto attr = buildingNode.attribute(name);

        if (attr.empty())
            return nullptr;

        return getTileEntry(attr.as_int());
    }

    const TileDefinition::TileData *getTile(uint32_t index, const std::string &_enum) const
    {
        if (index == 0)
            return nullptr;

        auto &tileEntry = tileEntries[index - 1];

        auto it = tileEntry.tiles.find(_enum);
        if (it == tileEntry.tiles.end())
        {
            return nullptr;
        }

        return it->second.tileData;
    }

    const TileDefinition::TileData *getUserTile(uint32_t index)
    {
        if (index == 0)
            return nullptr;

        if (index - 1 >= userTiles.size())
        {
            logger.warning("UserTile out of bounds: {} / {}", index, userTiles.size());
            return nullptr;
        }

        return userTiles[index - 1];
    }

    const BuildingRoom *getRoom(uint32_t roomId) const
    {
        if (roomId == 0)
        {
            return nullptr;
        }

        return &rooms[roomId - 1];
    }

    BuildingFloor *getFloor(uint32_t index)
    {
        if (index >= 0 && index < floors.size())
        {
            return &floors[index];
        }

        return nullptr;
    }

    const FurnitureTiles *getFurniture(uint32_t index) const
    {
        Assert::indexInBounds(index, furnitures.size());

        return &furnitures[index];
    }

    void readTileEntries(const pugi::xml_node &buildingNode)
    {
        for (const auto &child : buildingNode.children("tile_entry"))
        {
            BuildingTileEntry tileEntry;
            tileEntry.category = child.attribute("category").as_string();

            for (const auto &tileNode : child.children("tile"))
            {
                auto tileEnum = tileNode.attribute("enum").as_string();
                auto tileName = tileNode.attribute("tile").as_string();
                auto tileOffset = tileNode.attribute("offset").as_string();

                BuildingTile &buildingTile = tileEntry.tiles[tileEnum];

                buildingTile.tileData = tilesManager->getTileDataByName(formatTileName(tileName));
                buildingTile.tileOffset = Vector2i(tileOffset);
            }

            tileEntries.push_back(tileEntry);
        }

        exteriorWall = getTileEntry(buildingNode, "ExteriorWall");
        exteriorWallTrim = getTileEntry(buildingNode, "ExteriorWallTrim");
        door = getTileEntry(buildingNode, "Door");
        doorFrame = getTileEntry(buildingNode, "DoorFrame");
        window = getTileEntry(buildingNode, "Window");
        curtains = getTileEntry(buildingNode, "Curtains");
        shutters = getTileEntry(buildingNode, "Shutters");
        stairs = getTileEntry(buildingNode, "Stairs");
        roofCap = getTileEntry(buildingNode, "RoofCap");
        roofSlope = getTileEntry(buildingNode, "RoofSlope");
        roofTop = getTileEntry(buildingNode, "RoofTop");
        grimeWall = getTileEntry(buildingNode, "GrimeWall");
    }

    void readFurnitures(const pugi::xml_node &buildingNode)
    {
        for (const auto &furnitureNode : buildingNode.children("furniture"))
        {
            FurnitureTiles furnitureTiles;

            furnitureTiles.corners = furnitureNode.attribute("corners").as_bool();
            furnitureTiles.setLayer(furnitureNode);

            for (const auto &entryNode : furnitureNode.children("entry"))
            {
                Direction orient = getDirection(entryNode.attribute("orient").as_string());

                FurnitureTile &furnitureTile = furnitureTiles.tiles[orient];

                if (!entryNode.attribute("grime").empty())
                {
                    furnitureTile.grime = entryNode.attribute("grime").as_bool();
                }

                for (const auto &tileNode : entryNode.children("tile"))
                {
                    const std::string tilename = tileNode.attribute("name").as_string();
                    const int x = tileNode.attribute("x").as_int();
                    const int y = tileNode.attribute("y").as_int();

                    furnitureTile.addTile(x, y, tilesManager->getTileDataByName(formatTileName(tilename)));
                }
            }

            furnitures.push_back(furnitureTiles);
        }
    }

    void readRooms(const pugi::xml_node &buildingNode)
    {
        for (const auto &roomNode : buildingNode.children("room"))
        {
            rooms.emplace_back();

            BuildingRoom &room = rooms.back();

            room.name = roomNode.attribute("Name").as_string();
            room.internalName = roomNode.attribute("InternalName").as_string();

            room.interiorWall = roomNode.attribute("InteriorWall").as_int();
            room.interiorWallTrim = roomNode.attribute("InteriorWallTrim").as_int();
            room.floor = roomNode.attribute("Floor").as_int();
            room.grimeFloor = roomNode.attribute("GrimeFloor").as_int();
            room.grimeWall = roomNode.attribute("GrimeWall").as_int();
        }
    }

    void readFloors(const pugi::xml_node &buildingNode)
    {
        maxLayer = minLayer;

        for (const auto &floorNode : buildingNode.children("floor"))
        {
            floors.emplace_back(this, maxLayer);

            BuildingFloor &buildingFloor = floors.back();

            for (const auto &roomNode : floorNode.children("rooms"))
                buildingFloor.readRoom(roomNode);

            for (const auto &objectNode : floorNode.children("object"))
                buildingFloor.readObject(objectNode);

            for (const auto &tilesNode : floorNode.children("tiles"))
                buildingFloor.readTiles(tilesNode);

            maxLayer++;
        }
    }

    void readUserTiles(const pugi::xml_node &buildingNode)
    {
        for (const auto &tileNode : buildingNode.child("user_tiles").children("tile"))
        {
            auto tilename = tileNode.attribute("tile").as_string();
            auto *tile = tilesManager->getTileDataByName(formatTileName(tilename));

            userTiles.push_back(tile);
        }
    }

    void readProperties(const pugi::xml_node &buildingNode)
    {
        for (const auto &propertyNode : buildingNode.child("properties").children("property"))
        {
            std::string key = propertyNode.attribute("name").as_string();
            std::string value = propertyNode.attribute("value").as_string();

            properties[key] = value;
        }
    }

    void read(const std::string &path)
    {
        read(File::readAllBytes(path));
    }

    void read(const BytesBuffer &buffer)
    {
        pugi::xml_parse_result result = doc.load_buffer(buffer.data(), buffer.size());

        if (!result)
        {
            throw std::runtime_error("failed parsing tbx file");
        }

        pugi::xml_node buildingNode = doc.child("building");

        version = buildingNode.attribute("version").as_int();
        width = buildingNode.attribute("width").as_int();
        height = buildingNode.attribute("height").as_int();

        readProperties(buildingNode);
        readTileEntries(buildingNode);
        readFurnitures(buildingNode);
        readUserTiles(buildingNode);
        readRooms(buildingNode);
        readFloors(buildingNode);
    }

    void saveToPrefab(const std::string &path)
    {
        toPrefab().saveToFile(path);
    }

    void saveToFile(const std::string &path)
    {
        auto propNode = doc.child("building").child("properties");

        if(!propNode)
        {
            propNode = doc.child("building").prepend_child("properties");
        }

        propNode.remove_children();

        for (const auto &[key, value] : properties)
        {
            if(key.empty() && value.empty()) continue;

            auto node = propNode.append_child("property");

            node.append_attribute("name").set_value(key);
            node.append_attribute("value").set_value(value);
        }

        if (!doc.save_file(path.c_str()))
        {
            fmt::println("failed saving file at '{}'", path);
        }
    }

    void addTag(const std::string &tag)
    {
        auto tags = split(properties["tags"], ',');
        tags.insert(tag);
        properties["tags"] = join(tags, ",");
    }

    void removeTag(const std::string &tag)
    {
        auto tags = split(properties["tags"], ',');
        tags.erase(tag);
        properties["tags"] = join(tags, ",");
    }

    std::optional<Prefab> prefab = std::nullopt;

    Prefab toPrefab()
    {
        if (prefab.has_value()) return prefab.value();

        Assert::isTrue(width <= 255);
        Assert::isTrue(height <= 255);
        Assert::isTrue(minLayer < INT8_MAX);
        Assert::isTrue(maxLayer > INT8_MIN);

        std::unordered_map<const TileData *, uint32_t> tileToIndex;
        std::vector<int32_t> tilesBuffer;

        tilesBuffer.reserve(Square::MaxSection);

        Prefab prefab;

        prefab.minLayer = minLayer;
        prefab.maxLayer = maxLayer;
        prefab.width = width;
        prefab.height = height;

        for (BuildingFloor &floor : floors)
        {
            auto rectangles = floor.getRoomRectangles();

            for (const auto &[roomId, rects] : rectangles)
            {
                auto *floorRoom = getRoom(roomId);

                LotHeader::Room room;

                room.id = roomId;
                room.layer = floor.layer;
                room.name = floorRoom->name;

                for (const auto &rect : rects)
                {
                    room.rectangles.push_back(rect);
                }

                prefab.rooms.push_back(room);
            }

            floor.toSquares();

            for (int x = 0; x < floor.width; x++)
            {
                for (int y = 0; y < floor.height; y++)
                {
                    tilesBuffer.clear();

                    for (const TileData *tileData : floor.square(x, y).tiles)
                    {
                        if (tileData != nullptr)
                        {
                            auto [it, inserted] = tileToIndex.try_emplace(tileData, tileToIndex.size());
                            tilesBuffer.push_back(it->second);
                        }
                    }

                    if (tilesBuffer.empty())
                        continue;

                    prefab.squareMap.squares.emplace_back();

                    Lotpack::SquareData &square = prefab.squareMap.squares.back();

                    square.coord.x = x;
                    square.coord.y = y;
                    square.coord.z = floor.layer;

                    square.tileStart = prefab.squareMap.tiles.size();
                    square.tileCount = tilesBuffer.size();

                    for (const auto &tileIndex : tilesBuffer)
                    {
                        prefab.squareMap.tiles.push_back(tileIndex);
                    }

                    prefab.xmin = std::min(x, prefab.xmin);
                    prefab.ymin = std::min(y, prefab.ymin);
                    prefab.xmax = std::max(x, prefab.xmax);
                    prefab.ymax = std::max(y, prefab.ymax);
                }
            }
        }

        prefab.tileNames.resize(tileToIndex.size());

        Assert::isFalse(prefab.isEmpty());

        for (const auto &[tile, tileIndex] : tileToIndex)
        {
            prefab.tileNames[tileIndex] = tile->name;
        }

        this->prefab = prefab;

        return prefab;
    }
};
