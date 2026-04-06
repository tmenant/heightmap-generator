#pragma once

#include "FastNoise/FastNoise.h"
#include "FastNoise/Utility/SmartNode.h"
#include "command_manager.h"
#include "core/logger.h"
#include "core/random.h"
#include "core/timer.h"
#include "core/raw_image.h"
#include "core/color.h"
#include <vector>

class CmdHeightMap : public BaseCommand, public Loggable<CmdHeightMap>
{
    int exportResolution = 1024 * 1;
    int baseResolution = 2048;

    std::vector<float> heightMap;

    Random rand = Random(1337);

public:
    CLI::App *registerCommand(CLI::App &app) override
    {
        return app.add_subcommand("height-map", "");
    }

    std::string readNode()
    {
        std::string path = "ignore/noise.txt";
        std::ifstream file(path);

        if (!file.is_open())
        {
            std::cerr << "Erreur : Impossible d'ouvrir le fichier " << path << std::endl;
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        std::string content = buffer.str();

        content.erase(content.find_last_not_of(" \n\r\t") + 1);

        return content;
    }

    void executeCommand(AppContext &appContext) override
    {
        Timer timer;

        std::vector<float> noiseMap(exportResolution * exportResolution);
        heightMap.resize(noiseMap.size());

        std::string encodedNode = readNode();
        auto smartNode = FastNoise::NewFromEncodedNodeTree(encodedNode.c_str());

        float frequency = baseResolution / (float)exportResolution;

        smartNode->GenUniformGrid2D(noiseMap.data(), 0, 0, exportResolution, exportResolution, frequency, frequency, rand.nextInt32(0, INT32_MAX));

        for (uint32_t x = 0; x < exportResolution; x++)
        {
            for (uint32_t y = 0; y < exportResolution; y++)
            {
                int index = x + y * exportResolution;

                float gradient = this->gradient(x, y);
                float noise = noiseMap[index];
                float normalized = std::clamp(0.5f * (noise + 1.f), 0.f, 1.f);
                float value = normalized * gradient;

                heightMap[index] = value;
            }
        }

        logger.info("heightMap created: {}", timer.elapsedSeconds());

        saveToImage();

        logger.info("image created: {}", timer.elapsedSeconds());
    }

    float gradient(int x, int y) const
    {
        float nx = 2.f * x / exportResolution - 1.f;
        float ny = 2.f * y / exportResolution - 1.f;

        float distance = std::sqrt(nx * nx + ny * ny);

        return std::clamp(1.f - distance, 0.f, 1.f);
    }

    void saveToImage()
    {
        RawImage rawNoiseImage(exportResolution, exportResolution);
        RawImage colorNoiseImage(exportResolution, exportResolution);

        for (uint32_t x = 0; x < exportResolution; x++)
        {
            for (uint32_t y = 0; y < exportResolution; y++)
            {
                int index = x + y * exportResolution;
                int offset = index * 4;

                float greyScale = std::clamp(heightMap[index] * 255.f, 0.f, 255.f);

                rawNoiseImage.pixels[offset + 0] = greyScale;
                rawNoiseImage.pixels[offset + 1] = greyScale;
                rawNoiseImage.pixels[offset + 2] = greyScale;
                rawNoiseImage.pixels[offset + 3] = 255;

                Color color = getColorAt(index);

                colorNoiseImage.pixels[offset + 0] = color.r;
                colorNoiseImage.pixels[offset + 1] = color.g;
                colorNoiseImage.pixels[offset + 2] = color.b;
                colorNoiseImage.pixels[offset + 3] = color.a;
            }
        }

        rawNoiseImage.saveToEXR("ignore/heightmap_raw.exr");
        colorNoiseImage.saveToPNG("ignore/heightmap_colored.png");
    }

    Color getColorAt(int index)
    {
        static constexpr Color colorWater = { 108, 127, 131 };
        static constexpr Color colorDirt = { 91, 63, 21 };
        static constexpr Color colorGrass = { 75, 88, 27 };

        if (heightMap[index] < 0.15f)
        {
            return colorWater;
        }

        return colorGrass;
    }
};