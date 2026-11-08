#include <opencv2/opencv.hpp>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

// Struktur for å holde oversikt over musestatus og koordinater
struct SelectionState {
    cv::Mat image;
    cv::Rect roi;
    bool isSelected = false;
    const int cropSize = 640;
};

// Callback-funksjon for musen
void onMouse(int event, int x, int y, int flags, void* userdata) {
    SelectionState* state = static_cast<SelectionState*>(userdata);

    int xTopLeft = x - state->cropSize / 2;
    int yTopLeft = y - state->cropSize / 2;

    if (xTopLeft < 0) xTopLeft = 0;
    if (yTopLeft < 0) yTopLeft = 0;
    if (xTopLeft + state->cropSize > state->image.cols) xTopLeft = state->image.cols - state->cropSize;
    if (yTopLeft + state->cropSize > state->image.rows) yTopLeft = state->image.rows - state->cropSize;

    state->roi = cv::Rect(xTopLeft, yTopLeft, state->cropSize, state->cropSize);

    if (event == cv::EVENT_MOUSEMOVE && !state->isSelected) {
        cv::Mat canvas = state->image.clone();
        cv::rectangle(canvas, state->roi, cv::Scalar(0, 255, 0), 2);
        cv::imshow("Crop Tool (Click: Confirm | ESC: Skip | Q: Quit)", canvas);
    }
    else if (event == cv::EVENT_LBUTTONDOWN) {
        state->isSelected = true;
    }
}

int main(int argc, char* argv[]) {
    // Sjekk om brukeren har oppgitt input-mappe som argument
    if (argc < 2) {
        std::cout << "Bruk: " << argv[0] << " <sti_til_input_mappe>" << std::endl;
        return -1;
    }

    std::string inputDir = argv[1];
    std::string outputDir = "cropped_images/"; // Lagrer i en fast undermappe der programmet kjøres

    // Sjekk om input-mappen faktisk eksisterer
    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        std::cerr << "Feil: Mappen '" << inputDir << "' eksisterer ikke eller er ikke en gyldig mappe." << std::endl;
        return -1;
    }

    // Opprett utdatamappe hvis den ikke eksisterer
    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
    }

    // Støttede filendelser
    std::vector<std::string> extensions = { ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".webp" };

    std::string windowName = "Crop Tool (Click: Confirm | ESC: Skip | Q: Quit)";
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);

    std::cout << "Skanner bilder i: " << inputDir << std::endl;

    // Loop gjennom alle filer i mappen
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) continue;

        std::string filename = entry.path().filename().string();
        std::cout << "\nÅpner: " << filename << std::endl;

        cv::Mat img = cv::imread(entry.path().string());
        if (img.empty()) {
            std::cout << "Kunne ikke laste: " << filename << std::endl;
            continue;
        }

        if (img.cols < 640 || img.rows < 640) {
            std::cout << "Hoppet over (for lite bilde): " << filename << " (" << img.cols << "x" << img.rows << ")" << std::endl;
            continue;
        }

        SelectionState state;
        state.image = img;
        state.roi = cv::Rect(0, 0, 640, 640);
        state.isSelected = false;

        cv::Mat initialCanvas = img.clone();
        cv::rectangle(initialCanvas, state.roi, cv::Scalar(0, 255, 0), 2);
        cv::imshow(windowName, initialCanvas);

        cv::setMouseCallback(windowName, onMouse, &state);

        bool skipImage = false;
        bool quitProgram = false;

        // Løkke for å vente på input fra bruker
        while (!state.isSelected) {
            int key = cv::waitKey(10);

            if (key == 27) { // ESC hopper over dette bildet
                std::cout << "Hoppet over av bruker." << std::endl;
                skipImage = true;
                break;
            }
            if (key == 'q' || key == 'Q') { // Q avslutter hele programmet
                std::cout << "Avbryter programmet..." << std::endl;
                quitProgram = true;
                break;
            }
        }

        if (quitProgram) break;
        if (skipImage) continue;

        // Crop og lagre hvis brukeren klikket
        cv::Mat croppedImg = img(state.roi);
        std::string outputPath = outputDir + "crop_" + filename;
        cv::imwrite(outputPath, croppedImg);
        std::cout << "Lagret: " << outputPath << std::endl;
    }

    std::cout << "\nFerdig prosessert!" << std::endl;
    cv::destroyAllWindows();
    return 0;
}
