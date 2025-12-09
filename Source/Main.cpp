#define _CRT_SECURE_NO_WARNINGS

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <cstring>

#include "../Header/Util.h"

// ============================================================================
// KONSTANTE
// ============================================================================
const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
const float TARGET_FPS = 75.0f;
const float FRAME_TIME = 1.0f / TARGET_FPS;

const int NUM_SEATS = 8;
const float PI = 3.14159265359f;

// Fizika kretanja
const float MAX_SPEED = 0.25f;
const float ACCELERATION = 0.15f;
const float DECELERATION = 0.1f;
const float SLOW_RETURN_SPEED = 0.08f;
const float STOP_DURATION = 10.0f;

// ============================================================================
// STRUKTURE PODATAKA
// ============================================================================
struct Passenger {
    bool exists = false;
    bool belted = false;
    bool sick = false;
};

enum class GameState {
    LOADING_PASSENGERS,
    RUNNING,
    STOPPING,
    STOPPED,
    RETURNING,
    UNLOADING
};

// ============================================================================
// GLOBALNE PROMENLJIVE
// ============================================================================
GLFWwindow* window = nullptr;

// Shaderi
unsigned int basicShader;
unsigned int textureShader;

// VAO/VBO za osnovne oblike (boje)
unsigned int basicVAO, basicVBO;

// VAO/VBO za teksture
unsigned int texVAO, texVBO;

// Teksture
unsigned int texPassenger;
unsigned int texSick;
unsigned int texBelt;
unsigned int texCart;
unsigned int texInfo;

// Stanje igre
GameState gameState = GameState::LOADING_PASSENGERS;
Passenger passengers[NUM_SEATS];
int passengerCount = 0;

// Pozicija vozila na stazi (0.0 - 1.0)
float trackPosition = 0.0f;
float currentSpeed = 0.0f;
float stopTimer = 0.0f;

// Mis
double mouseX, mouseY;
bool mouseClicked = false;

// Uniformi - basic shader
int uModelLocBasic, uProjectionLocBasic, uAlphaLocBasic;

// Uniformi - texture shader
int uModelLocTex, uProjectionLocTex, uAlphaLocTex;

// Projection matrica (globalna za oba shadera)
float projectionMatrix[16];

// ============================================================================
// POMOCNE FUNKCIJE ZA STAZU (HORIZONTALNA SA 3 BREGA)
// ============================================================================
float getTrackX(float t) {
    // X ide od leve strane (-1.6) do desne (1.6)
    return -1.6f + t * 3.2f;
}

float getTrackY(float t) {
    // Sinusoida sa 3 brega (3 vrha)
    float baseY = -0.5f;
    float amplitude = 0.4f;

    // 3 brega = sin(3 * 2 * PI * t) daje 3 pune periode
    float wave = sinf(t * 6.0f * PI);

    return baseY + amplitude * (1.0f + wave) * 0.5f;
}

// Nagib staze za fiziku
float getTrackDerivativeY(float t) {
    float amplitude = 0.4f;
    float dWave = 6.0f * PI * cosf(t * 6.0f * PI);
    return amplitude * 0.5f * dWave;
}

bool isUphill(float t) {
    return getTrackDerivativeY(t) > 0.5f;
}

bool isDownhill(float t) {
    return getTrackDerivativeY(t) < -0.5f;
}

float getTrackAngle(float t) {
    // Racunaj nagib iz derivata
    float dx = 3.0f;  // dX/dt = 3.0 (konstantno)
    float dy = getTrackDerivativeY(t);
    return atan2f(dy, dx);
}

// ============================================================================
// FUNKCIJE ZA MATRICE
// ============================================================================
void setModelMatrix(int location, float x, float y, float scaleX, float scaleY, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float model[16] = {
        scaleX * c, scaleX * s, 0, 0,
        -scaleY * s, scaleY * c, 0, 0,
        0, 0, 1, 0,
        x, y, 0, 1
    };
    glUniformMatrix4fv(location, 1, GL_FALSE, model);
}

void setIdentityModel(int location) {
    float model[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    glUniformMatrix4fv(location, 1, GL_FALSE, model);
}

// ============================================================================
// UCITAVANJE TEKSTURA
// ============================================================================
unsigned int loadTextureWithPath(const char* filename) {
    std::string path = std::string("Resources/") + filename;

    unsigned int texture = loadImageToTexture(path.c_str());
    if (texture != 0) {
        // Postavi texture parametre
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        std::cout << "Ucitana tekstura: " << path << std::endl;
        return texture;
    }

    std::cout << "Greska: Nije pronadjena tekstura " << path << std::endl;
    return 0;
}

// ============================================================================
// CRTANJE - BASIC SHADER (linije, geometrija)
// ============================================================================
void drawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float thickness = 0.005f) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;

    float nx = -dy / len * thickness;
    float ny = dx / len * thickness;

    float vertices[] = {
        x1 + nx, y1 + ny, r, g, b, 1.0f,
        x1 - nx, y1 - ny, r, g, b, 1.0f,
        x2 - nx, y2 - ny, r, g, b, 1.0f,
        x1 + nx, y1 + ny, r, g, b, 1.0f,
        x2 - nx, y2 - ny, r, g, b, 1.0f,
        x2 + nx, y2 + ny, r, g, b, 1.0f
    };

    glBindVertexArray(basicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, basicVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void drawRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
    float vertices[] = {
        x, y, r, g, b, a,
        x + w, y, r, g, b, a,
        x + w, y + h, r, g, b, a,
        x, y, r, g, b, a,
        x + w, y + h, r, g, b, a,
        x, y + h, r, g, b, a
    };

    glBindVertexArray(basicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, basicVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void drawCircle(float cx, float cy, float radius, float r, float g, float b, float a = 1.0f) {
    const int segments = 32;
    std::vector<float> vertices;

    vertices.push_back(cx);
    vertices.push_back(cy);
    vertices.push_back(r);
    vertices.push_back(g);
    vertices.push_back(b);
    vertices.push_back(a);

    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * PI * i / segments;
        vertices.push_back(cx + radius * cosf(angle));
        vertices.push_back(cy + radius * sinf(angle));
        vertices.push_back(r);
        vertices.push_back(g);
        vertices.push_back(b);
        vertices.push_back(a);
    }

    glBindVertexArray(basicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, basicVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
}

// ============================================================================
// CRTANJE - TEXTURE SHADER
// ============================================================================
void drawTexturedQuad(unsigned int texture, float x, float y, float w, float h) {
    float vertices[] = {
        // pozicija      // tex coords
        x, y,            0.0f, 0.0f,
        x + w, y,        1.0f, 0.0f,
        x + w, y + h,    1.0f, 1.0f,
        x, y,            0.0f, 0.0f,
        x + w, y + h,    1.0f, 1.0f,
        x, y + h,        0.0f, 1.0f
    };

    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(texVAO);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ============================================================================
// CRTANJE POZADINE (nebo i trava)
// ============================================================================
void drawBackground() {
    glUseProgram(basicShader);
    setIdentityModel(uModelLocBasic);
    glUniform1f(uAlphaLocBasic, 1.0f);

    // Nebo (gradijent od svetlo plave do bele)
    drawRect(-2.0f, -0.2f, 4.0f, 1.5f, 0.5f, 0.75f, 0.95f);

    // Oblaci
    drawCircle(-0.8f, 0.6f, 0.15f, 1.0f, 1.0f, 1.0f);
    drawCircle(-0.6f, 0.62f, 0.12f, 1.0f, 1.0f, 1.0f);
    drawCircle(-0.5f, 0.58f, 0.1f, 1.0f, 1.0f, 1.0f);

    drawCircle(0.5f, 0.7f, 0.18f, 1.0f, 1.0f, 1.0f);
    drawCircle(0.7f, 0.72f, 0.14f, 1.0f, 1.0f, 1.0f);
    drawCircle(0.85f, 0.68f, 0.1f, 1.0f, 1.0f, 1.0f);

    drawCircle(1.2f, 0.5f, 0.12f, 1.0f, 1.0f, 1.0f);
    drawCircle(1.35f, 0.52f, 0.1f, 1.0f, 1.0f, 1.0f);

    // Trava (zelena)
    drawRect(-2.0f, -1.0f, 4.0f, 0.8f, 0.4f, 0.7f, 0.3f);

    // Tamnija trava u prednjem planu
    drawRect(-2.0f, -1.0f, 4.0f, 0.4f, 0.3f, 0.6f, 0.2f);
}

// ============================================================================
// CRTANJE STAZE
// ============================================================================
void drawTrack() {
    glUseProgram(basicShader);
    setIdentityModel(uModelLocBasic);
    glUniform1f(uAlphaLocBasic, 1.0f);

    const int numSegments = 200;

    // Prvo nacrtaj vertikalne nosace (sivi stubovi)
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20;
        float x = getTrackX(t);
        float y = getTrackY(t);

        // Vertikalni stub od tla do sine
        drawLine(x, -0.6f, x, y - 0.02f, 0.5f, 0.5f, 0.55f, 0.015f);
    }

    // Dijagonalni nosaci (ukrsteni)
    for (int i = 0; i < 20; i++) {
        float t1 = (float)i / 20;
        float t2 = (float)(i + 1) / 20;
        float x1 = getTrackX(t1);
        float y1 = getTrackY(t1);
        float x2 = getTrackX(t2);
        float y2 = getTrackY(t2);

        // X-nosaci
        float midY = (y1 + y2) / 2 - 0.1f;
        if (midY > -0.55f) {
            drawLine(x1, y1 - 0.02f, x2, midY, 0.45f, 0.45f, 0.5f, 0.008f);
            drawLine(x2, y2 - 0.02f, x1, midY, 0.45f, 0.45f, 0.5f, 0.008f);
        }
    }

    // Crtaj sine (crvene, kao na slici)
    for (int i = 0; i < numSegments; i++) {
        float t1 = (float)i / numSegments;
        float t2 = (float)(i + 1) / numSegments;

        float x1 = getTrackX(t1);
        float y1 = getTrackY(t1);
        float x2 = getTrackX(t2);
        float y2 = getTrackY(t2);

        // Gornja sina (crvena)
        drawLine(x1, y1, x2, y2, 0.8f, 0.15f, 0.1f, 0.012f);

        // Donja sina (tamnija crvena)
        drawLine(x1, y1 - 0.025f, x2, y2 - 0.025f, 0.6f, 0.1f, 0.08f, 0.008f);
    }

    // Pragovi na sinama (tamno sivi)
    for (int i = 0; i < 80; i++) {
        float t = (float)i / 80;
        float x = getTrackX(t);
        float y = getTrackY(t);
        float angle = getTrackAngle(t);

        float c = cosf(angle);
        float s = sinf(angle);

        // Horizontalni prag
        float len = 0.02f;
        drawLine(x - len * s, y - 0.012f + len * c,
            x + len * s, y - 0.012f - len * c,
            0.3f, 0.3f, 0.35f, 0.006f);
    }
}

// ============================================================================
// CRTANJE VOZILA SA TEKSTURAMA
// ============================================================================
void drawVehicle(float t) {
    float x = getTrackX(t);
    float y = getTrackY(t);
    float angle = getTrackAngle(t);

    // Crtaj vozilo (cart.png)
    glUseProgram(textureShader);
    setModelMatrix(uModelLocTex, x, y + 0.04f, 1.0f, 1.0f, angle);
    glUniform1f(uAlphaLocTex, 1.0f);

    // Vozilo
    float cartW = 0.18f;
    float cartH = 0.07f;
    drawTexturedQuad(texCart, -cartW / 2, -cartH / 2, cartW, cartH);

    // Crtaj putnike
    for (int i = 0; i < NUM_SEATS; i++) {
        if (!passengers[i].exists) continue;

        // Pozicija sedista (4 napred, 4 pozadi - sada levo/desno)
        float seatX = -0.065f + (i % 4) * 0.042f;
        float seatY = (i < 4) ? 0.01f : 0.045f;

        // Velicina putnika
        float pw = 0.032f;
        float ph = 0.05f;

        // Odabir teksture (normalan ili bolestan)
        unsigned int passTex = passengers[i].sick ? texSick : texPassenger;

        glUniform1f(uAlphaLocTex, 1.0f);
        drawTexturedQuad(passTex, seatX - pw / 2, seatY, pw, ph);

        // Pojas ako je vezan
        if (passengers[i].belted) {
            float beltW = 0.028f;
            float beltH = 0.025f;
            drawTexturedQuad(texBelt, seatX - beltW / 2, seatY + 0.01f, beltW, beltH);
        }
    }

    setIdentityModel(uModelLocTex);
}

// ============================================================================
// CRTANJE INDIKATORA SEDISTA
// ============================================================================
void drawSeatIndicators(float t) {
    float x = getTrackX(t);
    float y = getTrackY(t);

    glUseProgram(basicShader);
    setModelMatrix(uModelLocBasic, x, y - 0.08f, 0.5f, 0.5f, 0);
    glUniform1f(uAlphaLocBasic, 0.8f);

    for (int i = 0; i < NUM_SEATS; i++) {
        float sx = -0.14f + i * 0.04f;
        float sy = 0.0f;

        if (passengers[i].exists) {
            if (passengers[i].sick) {
                drawCircle(sx, sy, 0.012f, 0.0f, 0.8f, 0.0f); // Zelen - bolestan
            }
            else if (passengers[i].belted) {
                drawCircle(sx, sy, 0.012f, 0.2f, 0.6f, 1.0f); // Plav - vezan
            }
            else {
                drawCircle(sx, sy, 0.012f, 1.0f, 0.3f, 0.3f); // Crven - nije vezan
            }
        }
        else {
            drawCircle(sx, sy, 0.012f, 0.3f, 0.3f, 0.3f); // Siv - prazan
        }
    }

    setIdentityModel(uModelLocBasic);
}

// ============================================================================
// CRTANJE INFO PANELA (ime studenta)
// ============================================================================
void drawStudentInfo() {
    glUseProgram(textureShader);
    setIdentityModel(uModelLocTex);
    glUniform1f(uAlphaLocTex, 0.85f);

    // info.png u donjem desnom uglu - povecano
    float infoW = 0.7f;
    float infoH = 0.17f;
    drawTexturedQuad(texInfo, 0.25f, -0.98f, infoW, infoH);
}

// ============================================================================
// CRTANJE UI INSTRUKCIJA
// ============================================================================
void drawInstructions() {
    glUseProgram(basicShader);
    setIdentityModel(uModelLocBasic);
    glUniform1f(uAlphaLocBasic, 0.7f);

    // Pozadina
    drawRect(-0.98f, 0.75f, 0.52f, 0.22f, 0.0f, 0.0f, 0.0f, 0.5f);

    // Indikator stanja
    float stateR = 0.5f, stateG = 0.5f, stateB = 0.5f;
    switch (gameState) {
    case GameState::LOADING_PASSENGERS:
        stateR = 0.0f; stateG = 1.0f; stateB = 0.0f;
        break;
    case GameState::RUNNING:
        stateR = 1.0f; stateG = 1.0f; stateB = 0.0f;
        break;
    case GameState::STOPPING:
    case GameState::STOPPED:
        stateR = 1.0f; stateG = 0.0f; stateB = 0.0f;
        break;
    case GameState::RETURNING:
        stateR = 1.0f; stateG = 0.5f; stateB = 0.0f;
        break;
    case GameState::UNLOADING:
        stateR = 0.5f; stateG = 0.5f; stateB = 1.0f;
        break;
    }

    glUniform1f(uAlphaLocBasic, 1.0f);
    drawCircle(-0.93f, 0.92f, 0.03f, stateR, stateG, stateB);

    // Brzina indikator
    float speedRatio = currentSpeed / MAX_SPEED;
    drawRect(-0.88f, 0.77f, 0.38f * speedRatio, 0.03f, 0.2f, 0.8f, 0.2f);
    drawRect(-0.88f, 0.77f, 0.38f, 0.03f, 0.3f, 0.3f, 0.3f, 0.3f);

    // Legenda (mali krugovi sa bojama)
    glUniform1f(uAlphaLocBasic, 0.9f);
    // Zelen = ukrcavanje
    drawCircle(-0.93f, 0.86f, 0.015f, 0.0f, 1.0f, 0.0f);
    // Zut = voznja
    drawCircle(-0.83f, 0.86f, 0.015f, 1.0f, 1.0f, 0.0f);
    // Crven = stop
    drawCircle(-0.73f, 0.86f, 0.015f, 1.0f, 0.0f, 0.0f);
    // Narandzast = povratak
    drawCircle(-0.63f, 0.86f, 0.015f, 1.0f, 0.5f, 0.0f);
    // Plav = iskrcavanje
    drawCircle(-0.53f, 0.86f, 0.015f, 0.5f, 0.5f, 1.0f);
}

// ============================================================================
// CALLBACK FUNKCIJE
// ============================================================================
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    if (action == GLFW_PRESS) {
        switch (gameState) {
        case GameState::LOADING_PASSENGERS:
            if (key == GLFW_KEY_SPACE) {
                for (int i = 0; i < NUM_SEATS; i++) {
                    if (!passengers[i].exists) {
                        passengers[i].exists = true;
                        passengers[i].belted = false;
                        passengers[i].sick = false;
                        passengerCount++;
                        break;
                    }
                }
            }
            else if (key == GLFW_KEY_ENTER) {
                bool allBelted = true;
                bool hasPassengers = false;
                for (int i = 0; i < NUM_SEATS; i++) {
                    if (passengers[i].exists) {
                        hasPassengers = true;
                        if (!passengers[i].belted) {
                            allBelted = false;
                            break;
                        }
                    }
                }

                if (hasPassengers && allBelted) {
                    gameState = GameState::RUNNING;
                    currentSpeed = 0.0f;
                }
            }
            break;

        case GameState::RUNNING:
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8) {
                int seatIndex = key - GLFW_KEY_1;
                if (passengers[seatIndex].exists && !passengers[seatIndex].sick) {
                    passengers[seatIndex].sick = true;
                    gameState = GameState::STOPPING;
                }
            }
            break;

        default:
            break;
        }
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        mouseClicked = true;
        glfwGetCursorPos(window, &mouseX, &mouseY);
    }
}

// ============================================================================
// PROVERA KLIKA NA PUTNIKA
// ============================================================================
bool isClickOnPassenger(int seatIndex, float clickX, float clickY) {
    float t = trackPosition;
    float vx = getTrackX(t);
    float vy = getTrackY(t) + 0.04f;  // Offset za vozilo
    float angle = getTrackAngle(t);

    float localSeatX = -0.065f + (seatIndex % 4) * 0.042f;
    float localSeatY = (seatIndex < 4) ? 0.035f : 0.07f;

    float c = cosf(angle);
    float s = sinf(angle);
    float worldSeatX = vx + localSeatX * c - localSeatY * s;
    float worldSeatY = vy + localSeatX * s + localSeatY * c;

    float dx = clickX - worldSeatX;
    float dy = clickY - worldSeatY;
    float dist = sqrtf(dx * dx + dy * dy);

    return dist < 0.05f;
}

void handleMouseClick() {
    if (!mouseClicked) return;
    mouseClicked = false;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    float aspect = (float)width / height;

    float clickX = ((float)(mouseX / width) * 2.0f - 1.0f) * aspect;
    float clickY = 1.0f - (float)(mouseY / height) * 2.0f;

    if (gameState == GameState::LOADING_PASSENGERS) {
        for (int i = 0; i < NUM_SEATS; i++) {
            if (passengers[i].exists && !passengers[i].belted) {
                if (isClickOnPassenger(i, clickX, clickY)) {
                    passengers[i].belted = true;
                    break;
                }
            }
        }
    }
    else if (gameState == GameState::UNLOADING) {
        for (int i = 0; i < NUM_SEATS; i++) {
            if (passengers[i].exists) {
                if (isClickOnPassenger(i, clickX, clickY)) {
                    passengers[i].exists = false;
                    passengers[i].belted = false;
                    passengers[i].sick = false;
                    passengerCount--;

                    bool anyLeft = false;
                    for (int j = 0; j < NUM_SEATS; j++) {
                        if (passengers[j].exists) {
                            anyLeft = true;
                            break;
                        }
                    }
                    if (!anyLeft) {
                        gameState = GameState::LOADING_PASSENGERS;
                    }
                    break;
                }
            }
        }
    }
}

// ============================================================================
// AZURIRANJE FIZIKE
// ============================================================================
void updatePhysics(float deltaTime) {
    switch (gameState) {
    case GameState::RUNNING:
    {
        // Koristi nagib staze za ubrzanje/usporenje
        float slope = getTrackDerivativeY(trackPosition);

        // Smanjen efekat gravitacije
        float gravityEffect = -slope * 0.15f;

        currentSpeed += gravityEffect * deltaTime;

        // Razlicite ciljne brzine zavisno od terena
        float targetSpeed;

        if (slope < -0.3f) {
            // Nizbrdica - najveca brzina
            targetSpeed = MAX_SPEED;
        }
        else if (slope > 0.3f) {
            // Uzbrdica - najmanja brzina
            targetSpeed = 0.08f;
        }
        else {
            // Prelaz/ravno - srednja brzina
            targetSpeed = MAX_SPEED * 0.6f;
        }

        // Lagano prilagodi brzinu ka ciljnoj
        if (currentSpeed < targetSpeed) {
            currentSpeed += ACCELERATION * 0.2f * deltaTime;
        }
        else if (currentSpeed > targetSpeed) {
            currentSpeed -= DECELERATION * 0.15f * deltaTime;
        }

        // Ogranici brzinu
        if (currentSpeed > MAX_SPEED) currentSpeed = MAX_SPEED;
        if (currentSpeed < 0.06f) currentSpeed = 0.06f;

        trackPosition += currentSpeed * deltaTime;

        if (trackPosition >= 1.0f) {
            trackPosition = 1.0f;
            gameState = GameState::RETURNING;
        }
    }
    break;

    case GameState::STOPPING:
        currentSpeed -= DECELERATION * 2.0f * deltaTime;
        if (currentSpeed <= 0) {
            currentSpeed = 0;
            gameState = GameState::STOPPED;
            stopTimer = 0;
        }
        trackPosition += currentSpeed * deltaTime;
        break;

    case GameState::STOPPED:
        stopTimer += deltaTime;
        if (stopTimer >= STOP_DURATION) {
            gameState = GameState::RETURNING;
        }
        break;

    case GameState::RETURNING:
        currentSpeed = SLOW_RETURN_SPEED;
        trackPosition -= currentSpeed * deltaTime;

        if (trackPosition <= 0) {
            trackPosition = 0;
            currentSpeed = 0;

            for (int i = 0; i < NUM_SEATS; i++) {
                passengers[i].belted = false;
            }

            gameState = GameState::UNLOADING;
        }
        break;

    default:
        break;
    }
}

// ============================================================================
// KREIRANJE KURSORA IZ SLIKE
// ============================================================================
GLFWcursor* createCursorWithPath(const char* filename) {
    std::string path = std::string("Resources/") + filename;

    GLFWcursor* cursor = loadImageToCursor(path.c_str());
    if (cursor) {
        std::cout << "Ucitan kursor: " << path << std::endl;
        return cursor;
    }

    std::cout << "Greska: Nije pronadjen kursor " << path << std::endl;
    return nullptr;
}

// ============================================================================
// KOMPILACIJA SEJDERA (lokalna verzija)
// ============================================================================
unsigned int compileShaderLocal(GLenum type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "Shader greska: " << infoLog << std::endl;
    }

    return shader;
}

unsigned int createShaderProgramLocal(const char* vertexSource, const char* fragmentSource) {
    unsigned int vertexShader = compileShaderLocal(GL_VERTEX_SHADER, vertexSource);
    unsigned int fragmentShader = compileShaderLocal(GL_FRAGMENT_SHADER, fragmentSource);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "Shader linking greska: " << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    if (!glfwInit()) {
        std::cout << "GLFW greska!" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Fullscreen
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    window = glfwCreateWindow(mode->width, mode->height, "Rolerkoster - Aleksa Doroslovac RA9/2022", monitor, NULL);
    if (!window) {
        std::cout << "Prozor greska!" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) {
        std::cout << "GLEW greska!" << std::endl;
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL verzija: " << glGetString(GL_VERSION) << std::endl;

    // Callbacks
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    // Kursor iz slike
    GLFWcursor* cursor = createCursorWithPath("cursor.png");
    if (cursor) {
        glfwSetCursor(window, cursor);
    }

    // Blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ========================================================================
    // BASIC SHADER (za linije i geometriju)
    // ========================================================================
    const char* basicVS = R"(
        #version 330 core
        layout(location = 0) in vec2 inPos;
        layout(location = 1) in vec4 inCol;
        out vec4 channelCol;
        uniform mat4 uModel;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * uModel * vec4(inPos, 0.0, 1.0);
            channelCol = inCol;
        }
    )";

    const char* basicFS = R"(
        #version 330 core
        in vec4 channelCol;
        out vec4 outCol;
        uniform float uAlpha;
        void main() {
            outCol = vec4(channelCol.rgb, channelCol.a * uAlpha);
        }
    )";

    basicShader = createShaderProgramLocal(basicVS, basicFS);
    uModelLocBasic = glGetUniformLocation(basicShader, "uModel");
    uProjectionLocBasic = glGetUniformLocation(basicShader, "uProjection");
    uAlphaLocBasic = glGetUniformLocation(basicShader, "uAlpha");

    // ========================================================================
    // TEXTURE SHADER
    // ========================================================================
    const char* texVS = R"(
        #version 330 core
        layout(location = 0) in vec2 inPos;
        layout(location = 1) in vec2 inTex;
        out vec2 chTex;
        uniform mat4 uModel;
        uniform mat4 uProjection;
        void main() {
            gl_Position = uProjection * uModel * vec4(inPos, 0.0, 1.0);
            chTex = inTex;
        }
    )";

    const char* texFS = R"(
        #version 330 core
        in vec2 chTex;
        out vec4 outCol;
        uniform sampler2D uTex;
        uniform float uAlpha;
        void main() {
            vec4 texColor = texture(uTex, chTex);
            outCol = vec4(texColor.rgb, texColor.a * uAlpha);
        }
    )";

    textureShader = createShaderProgramLocal(texVS, texFS);
    uModelLocTex = glGetUniformLocation(textureShader, "uModel");
    uProjectionLocTex = glGetUniformLocation(textureShader, "uProjection");
    uAlphaLocTex = glGetUniformLocation(textureShader, "uAlpha");

    // ========================================================================
    // PROJECTION MATRIX
    // ========================================================================
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    float aspect = (float)width / height;

    float projection[16] = {
        1.0f / aspect, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    glUseProgram(basicShader);
    glUniformMatrix4fv(uProjectionLocBasic, 1, GL_FALSE, projection);

    glUseProgram(textureShader);
    glUniformMatrix4fv(uProjectionLocTex, 1, GL_FALSE, projection);

    // ========================================================================
    // VAO/VBO SETUP - BASIC (pozicija + boja)
    // ========================================================================
    glGenVertexArrays(1, &basicVAO);
    glGenBuffers(1, &basicVBO);

    glBindVertexArray(basicVAO);
    glBindBuffer(GL_ARRAY_BUFFER, basicVBO);

    // layout: pos(2) + color(4) = 6 floats
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // ========================================================================
    // VAO/VBO SETUP - TEXTURE (pozicija + texcoord)
    // ========================================================================
    glGenVertexArrays(1, &texVAO);
    glGenBuffers(1, &texVBO);

    glBindVertexArray(texVAO);
    glBindBuffer(GL_ARRAY_BUFFER, texVBO);

    // layout: pos(2) + tex(2) = 4 floats
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // ========================================================================
    // UCITAVANJE TEKSTURA
    // ========================================================================
    texPassenger = loadTextureWithPath("passenger.png");
    texSick = loadTextureWithPath("sick.png");
    texBelt = loadTextureWithPath("belt.png");
    texCart = loadTextureWithPath("cart.png");
    texInfo = loadTextureWithPath("info.png");

    // Pozadina
    glClearColor(0.4f, 0.7f, 0.9f, 1.0f);

    // Frame timing
    double lastTime = glfwGetTime();

    // ========================================================================
    // GLAVNA PETLJA
    // ========================================================================
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;

        // Frame limiter 75 FPS
        if (deltaTime < FRAME_TIME) {
            continue;
        }
        lastTime = currentTime;

        glfwPollEvents();
        handleMouseClick();
        updatePhysics((float)deltaTime);

        glClear(GL_COLOR_BUFFER_BIT);

        // Crtanje pozadine (nebo i trava)
        drawBackground();

        // Crtanje staze
        drawTrack();

        // Crtanje vozila sa teksturama
        drawVehicle(trackPosition);

        // Indikatori sedista
        drawSeatIndicators(trackPosition);

        // UI
        drawInstructions();
        drawStudentInfo();

        glfwSwapBuffers(window);
    }

    // Cleanup
    glDeleteVertexArrays(1, &basicVAO);
    glDeleteBuffers(1, &basicVBO);
    glDeleteVertexArrays(1, &texVAO);
    glDeleteBuffers(1, &texVBO);
    glDeleteProgram(basicShader);
    glDeleteProgram(textureShader);

    glDeleteTextures(1, &texPassenger);
    glDeleteTextures(1, &texSick);
    glDeleteTextures(1, &texBelt);
    glDeleteTextures(1, &texCart);
    glDeleteTextures(1, &texInfo);

    if (cursor) glfwDestroyCursor(cursor);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
