#pragma once

#include "FastNoise/FastNoise.h"
#include "FastNoise/Utility/SmartNode.h"
#include "command_manager.h"
#include "core/color.h"
#include "core/file.h"
#include "core/logger.h"
#include "core/random.h"
#include "core/raw_image.h"
#include "core/timer.h"
#include "fmt/base.h"
#include <cstdint>
#include <pugixml.hpp>
#include <vector>

struct Settings
{
    std::string baseNoise = "";
    std::string landNoise = "";

    int exportResolution = 2048;
    int baseResolution = 1024;
    int seed = 1337;

    float elevationScale = 0.50f;
    float waterLevel = 0.15f;

    Settings(const std::string &filename)
    {
        auto buffer = File::readAllBytes(filename);

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(buffer.data(), buffer.size());

        if (!result)
        {
            throw std::runtime_error("failed parsing tbx file");
        }

        pugi::xml_node settingsNode = doc.child("settings");

        baseResolution = settingsNode.child("baseResolution").attribute("value").as_int();
        exportResolution = settingsNode.child("exportResolution").attribute("value").as_int();
        seed = settingsNode.child("seed").attribute("value").as_int();

        waterLevel = settingsNode.child("waterLevel").attribute("value").as_float();
        elevationScale = settingsNode.child("elevationScale").attribute("value").as_float();

        baseNoise = settingsNode.child("baseNoise").attribute("value").as_string();
        landNoise = settingsNode.child("landNoise").attribute("value").as_string();
    }
};

struct HeightMap
{
    Settings settings;

    std::vector<float> baseNoise;
    std::vector<float> landNoise;

    Random rand;

    int size = 0;

    HeightMap(const Settings &settings) : settings(settings)
    {
        size = settings.exportResolution;

        baseNoise.resize(size * size);
        landNoise.resize(size * size);

        if (settings.seed) rand = Random(settings.seed);
    }

    void Generate()
    {
        generateBaseNoise();
        generateLandNoise();

        saveColorMap("ignore/heightmap_colored.png");
        saveHeightMap("C:/Users/menan/Documents/terrain-demo/heightmap_raw.exr");
    }

    void generateBaseNoise()
    {
        auto smartNode = FastNoise::NewFromEncodedNodeTree(settings.baseNoise.c_str());

        float frequency = settings.baseResolution / (float)settings.exportResolution;

        std::vector<float> noiseMap(size * size);

        smartNode->GenUniformGrid2D(noiseMap.data(), 0, 0, size, size, frequency, frequency, rand.nextInt32(0, INT32_MAX));

        for (uint32_t x = 0; x < size; x++)
        {
            for (uint32_t y = 0; y < size; y++)
            {
                int index = x + y * size;

                float gradient = this->gradient(x, y);
                float noise = noiseMap[index];
                float normalized = normalize(noise);
                float value = normalized * gradient;

                baseNoise[index] = value;
            }
        }
    }

    void generateLandNoise()
    {
        auto smartNode = FastNoise::NewFromEncodedNodeTree(settings.landNoise.c_str());

        float frequency = settings.baseResolution / (float)settings.exportResolution;

        std::vector<float> noiseMap(size * size);

        int offsetX = 1024; // rand.nextInt32(0, INT32_MAX);
        int offsetY = 1024; // rand.nextInt32(0, INT32_MAX);

        smartNode->GenUniformGrid2D(noiseMap.data(), offsetX, offsetY, size, size, frequency, frequency, rand.nextInt32(0, INT32_MAX));

        for (uint32_t x = 0; x < size; x++)
        {
            for (uint32_t y = 0; y < size; y++)
            {
                int index = x + y * size;

                landNoise[index] = normalize(noiseMap[index]);
            }
        }
    }

    float normalize(float value)
    {
        return std::clamp(0.5f * (value + 1.f), 0.f, 1.f);
    }

    float gradient(int x, int y) const
    {
        float nx = 2.f * x / size - 1.f;
        float ny = 2.f * y / size - 1.f;

        float distance = std::sqrt(nx * nx + ny * ny);

        return std::clamp(1.f - distance, 0.f, 1.f);
    }

    void saveColorMap(const std::string &filename)
    {
        RawImage rawNoiseImage(size, size);
        RawImage colorNoiseImage(size, size);

        for (uint32_t x = 0; x < size; x++)
        {
            for (uint32_t y = 0; y < size; y++)
            {
                int index = x + y * size;
                int offset = index * 4;

                Color color = getColorAt(index);

                colorNoiseImage.pixels[offset + 0] = color.r;
                colorNoiseImage.pixels[offset + 1] = color.g;
                colorNoiseImage.pixels[offset + 2] = color.b;
                colorNoiseImage.pixels[offset + 3] = color.a;
            }
        }

        colorNoiseImage.saveToPNG(filename);
    }

    void saveHeightMap(const std::string &filename)
    {
        const int components = 4;

        std::vector<float> floatData(size * size * components);

        for (uint32_t y = 0; y < size; y++)
        {
            for (uint32_t x = 0; x < size; x++)
            {
                int index = x + y * size;
                int offset = index * 4;

                float height = getHeightAt(index);

                floatData[offset + 0] = height;
                floatData[offset + 1] = height;
                floatData[offset + 2] = height;
                floatData[offset + 3] = 255;
            }
        }

        const int save_as_fp16 = 0;
        const char *err = nullptr;

        int ret = SaveEXR(floatData.data(), size, size, components, save_as_fp16, filename.c_str(), &err);

        if (ret != TINYEXR_SUCCESS)
        {
            if (err)
            {
                fmt::println("{}", err);
                FreeEXRErrorMessage(err);
            }
            else
            {
                fmt::println("EXR error");
            }
        }
    }

    float inverseLerp(float a, float b, float value)
    {
        if (std::abs(a - b) < 0.00001f) return 0.0f;

        float result = (value - a) / (b - a);

        return std::clamp(result, 0.0f, 1.0f);
    }

    float getHeightAt(int index)
    {
        float mask = baseNoise[index];
        float waterLevel = settings.waterLevel;

        if (mask <= waterLevel)
        {
            return mask;
        }

        float landInfluence = inverseLerp(waterLevel, waterLevel + 0.2f, mask);
        float elevation = landNoise[index] * settings.elevationScale;

        return waterLevel + (elevation * landInfluence);
    }

    Color getColorAt(int index)
    {
        static constexpr Color colorWater = { 108, 127, 131 };
        static constexpr Color colorDirt = { 91, 63, 21 };
        static constexpr Color colorGrass = { 75, 88, 27 };

        if (baseNoise[index] < settings.waterLevel)
        {
            return colorWater;
        }

        return colorGrass;
    }
};

class CmdHeightMap : public BaseCommand, public Loggable<CmdHeightMap>
{
public:
    CLI::App *registerCommand(CLI::App &app) override
    {
        return app.add_subcommand("height-map", "");
    }

    void executeCommand(AppContext &appContext) override
    {
        Timer timer;

        Settings settings("settings.xml");
        HeightMap heightMap(settings);

        heightMap.Generate();

        logger.info("heightMap created in {}ms", timer.elapsedMiliseconds());
    }
};