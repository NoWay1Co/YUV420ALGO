#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <map>
#include <sstream>
#include <chrono>

using namespace std;

// Structure to represent an RGB pixel
struct RGB {
    uint8_t r, g, b;
};

// Structure to represent a YUV pixel
struct YUV {
    uint8_t y, u, v;
};

struct YUVPtr {
    uint8_t *y, *u, *v;
};

class Config {
public:
    Config(const string& configFile) {
        readConfig(configFile);
    }

    string getInputYuvFile() const { return inputYuvFile; }
    string getOutputYuvFile() const { return outputYuvFile; }
    string getBmpFile() const { return bmpFile; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

private:
    string inputYuvFile = "input.yuv";
    string outputYuvFile = "output.yuv";
    string bmpFile = "input.bmp";
    uint32_t width = 1920;
    uint32_t height = 1080;

    void readConfig(const string& configFile) {
        ifstream file(configFile);
        if (!file) {
            cerr << "Error opening config file: " << configFile << endl;
            exit(1);
        }

        string line;
        map<string, string> config;

        while (getline(file, line)) {
            stringstream ss(line);
            string key, value;
            if (getline(ss, key, '=') && getline(ss, value)) {
                config[key] = value;
            }
        }

        if (config.find("input_yuv") != config.end()) inputYuvFile = config["input_yuv"];
        if (config.find("output_yuv") != config.end()) outputYuvFile = config["output_yuv"];
        if (config.find("bmp_file") != config.end()) bmpFile = config["bmp_file"];
        if (config.find("width") != config.end()) width = stoi(config["width"]);
        if (config.find("height") != config.end()) height = stoi(config["height"]);

        file.close();
    }
};

class BMPReader {
public:
    void readBMP(const string& filename, vector<RGB>& data, uint32_t& width, uint32_t& height) {
        ifstream file(filename, ios::binary);
        if (!file) {
            cerr << "Error opening BMP file: " << filename << endl;
            exit(1);
        }

        uint8_t header[54];
        file.read(reinterpret_cast<char*>(header), 54);

        uint32_t pixelArrayOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
        file.seekg(pixelArrayOffset, ios::beg);

        // Check the BMP magic number to validate the file
        if (header[0] != 'B' || header[1] != 'M') {
            cerr << "Invalid BMP file: " << filename << endl;
            exit(1);
        }

        width = header[18] |
            (header[19] << 8) |
            (header[20] << 16) |
            (header[21] << 24);
        height = header[22] |
            (header[23] << 8) |
            (header[24] << 16) |
            (header[25] << 24);

        data.reserve(width * height);
        data.resize(width * height);

        uint32_t rowSize = (3 * width + 3) & ~3; // Align to 4 bytes (stride)
        vector<uint8_t> rowData(rowSize);

        for (int32_t i = height - 1; i >= 0; --i) {
            file.read(reinterpret_cast<char*>(rowData.data()), rowSize);
            for (uint32_t j = 0; j < width; ++j) {
                data[i * width + j] = { rowData[j * 3 + 2], rowData[j * 3 + 1], rowData[j * 3] };
            }
        }

        file.close();
    }
};

class YUVConverter {
public:
    void rgbToYUV420(const vector<RGB>& rgbData, vector<YUV>& yuvData, uint32_t width, uint32_t height) {
        yuvData.resize(width * height);

        auto worker = [&](uint32_t start, uint32_t end) {
            for (uint32_t i = start; i < end; ++i) {
                const RGB& rgb = rgbData[i];
                YUV& yuv = yuvData[i];

                yuv.y = static_cast<uint8_t>(0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b);
                yuv.u = static_cast<uint8_t>((-0.16874 * rgb.r - 0.33126 * rgb.g + 0.5 * rgb.b) + 128);
                yuv.v = static_cast<uint8_t>((0.5 * rgb.r - 0.41869 * rgb.g - 0.08131 * rgb.b) + 128);
            }
        };

        uint32_t numThreads = thread::hardware_concurrency();
        vector<thread> threads;
        uint32_t step = (width * height) / numThreads;

        for (uint32_t i = 0; i < numThreads; ++i) {
            uint32_t start = i * step;
            uint32_t end = (i == numThreads - 1) ? (width * height) : (start + step);
            threads.emplace_back(worker, start, end);
        }

        for (auto& t : threads) {
            t.join();
        }
    }
};

class YUVOverlay {
public:
    void overlayBMP(vector<YUVPtr>& yuvFrame, const vector<YUV>& bmpYUV, uint32_t frameWidth, uint32_t frameHeight, uint32_t bmpWidth, uint32_t bmpHeight) {
        for (uint32_t y = 0; y < bmpHeight; ++y) {
            for (uint32_t x = 0; x < bmpWidth; ++x) {
                uint32_t frameIndex = y * frameWidth + x;
                uint32_t bmpIndex = y * bmpWidth + x;
                *yuvFrame[frameIndex].y = bmpYUV[bmpIndex].y;
                *yuvFrame[frameIndex].u = bmpYUV[bmpIndex].u;
                *yuvFrame[frameIndex].v = bmpYUV[bmpIndex].v;
            }
        }
    }
};

class YUVProcessor {
public:
    YUVProcessor(const Config& config) : config(config) {}

    void process() {
        ifstream yuvFile(config.getInputYuvFile(), ios::binary);
        ofstream outputYuvFile(config.getOutputYuvFile(), ios::binary);

        if (!yuvFile || !outputYuvFile) {
            cerr << "Error opening YUV files." << endl;
            exit(1);
        }

        BMPReader bmpReader;
        vector<RGB> bmpData;
        uint32_t bmpWidth, bmpHeight;
        bmpReader.readBMP(config.getBmpFile(), bmpData, bmpWidth, bmpHeight);

        YUVConverter yuvConverter;
        vector<YUV> bmpYUV;
        yuvConverter.rgbToYUV420(bmpData, bmpYUV, bmpWidth, bmpHeight);

        vector<uint8_t> yPlane(config.getWidth() * config.getHeight());
        vector<uint8_t> uPlane((config.getWidth() / 2) * (config.getHeight() / 2));
        vector<uint8_t> vPlane((config.getWidth() / 2) * (config.getHeight() / 2));

        YUVOverlay yuvOverlay;
        while (yuvFile.read(reinterpret_cast<char*>(yPlane.data()), yPlane.size())) {
            yuvFile.read(reinterpret_cast<char*>(uPlane.data()), uPlane.size());
            yuvFile.read(reinterpret_cast<char*>(vPlane.data()), vPlane.size());

            vector<YUVPtr> frame(config.getWidth() * config.getHeight());
            for (uint32_t y = 0; y < config.getHeight(); ++y) {
                for (uint32_t x = 0; x < config.getWidth(); ++x) {
                    uint32_t index = y * config.getWidth() + x;
                    frame[index].y = &yPlane[index];
                    frame[index].u = &uPlane[(y / 2) * (config.getWidth() / 2) + (x / 2)];
                    frame[index].v = &vPlane[(y / 2) * (config.getWidth() / 2) + (x / 2)];
                }
            }

            yuvOverlay.overlayBMP(frame, bmpYUV, config.getWidth(), config.getHeight(), bmpWidth, bmpHeight);

            outputYuvFile.write(reinterpret_cast<const char*>(yPlane.data()), yPlane.size());
            outputYuvFile.write(reinterpret_cast<const char*>(uPlane.data()), uPlane.size());
            outputYuvFile.write(reinterpret_cast<const char*>(vPlane.data()), vPlane.size());
        }

        yuvFile.close();
        outputYuvFile.close();

        cout << "Program finished successfully." << endl;
    }

private:
    const Config& config;
};

int main() {
    // Checking how many hardware threads are available
    unsigned int numThreads = thread::hardware_concurrency();
    if (numThreads == 0) {
        cout << "Unable to determine the number of hardware threads." << endl;
    }
    else {
        cout << "Number of hardware threads available: " << numThreads << endl;
    }

    // Time tracking
    auto start = chrono::high_resolution_clock::now();

    Config config("config.txt");
    YUVProcessor processor(config);
    processor.process();

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end - start;
    cout << "Computing Time: " << duration.count() << " seconds" << endl;

    cin.get();
    return 0;
}
