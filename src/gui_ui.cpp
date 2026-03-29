#include "gui.h"
#include <algorithm>
#include <sstream>

void GUI::handleMouseDown(int x, int y) {
    // 文件浏览器模式
    if (m_uiState == UIState::FILE_BROWSER) {
        fbHandleClick(x, y);
        return;
    }

    // 检查按钮
    int btnIdx = hitButton(x, y);
    if (btnIdx >= 0) {
        switch (btnIdx) {
            case 0: // 新局
                newGame();
                break;
            case 1: // 悔棋
                undoLastMove();
                break;
            case 2: // 翻转
                m_settings.flipBoard = !m_settings.flipBoard;
                break;
            case 3: // 镜像
                m_settings.mirrorBoard = !m_settings.mirrorBoard;
                break;
            case 4: // 引擎执红
                m_settings.enginePlaysRed = !m_settings.enginePlaysRed;
                m_message = m_settings.enginePlaysRed ? "引擎执红: 开" : "引擎执红: 关";
                m_messageTime = SDL_GetTicks();
                initButtons();
                // 关闭后若引擎正在为已禁用的一方思考，立即停止
                if (m_engine.isThinking() && !isEngineTurn() && !m_analysisMode) {
                    m_ignoreNextBestMove = true;
                    m_engine.sendStop();
                    m_pvArrow1.valid = false;
                    m_pvArrow2.valid = false;
                    m_engineInfoText.clear();
                } else {
                    tryStartEngineMove();
                }
                break;
            case 5: // 引擎执黑
                m_settings.enginePlaysBlack = !m_settings.enginePlaysBlack;
                m_message = m_settings.enginePlaysBlack ? "引擎执黑: 开" : "引擎执黑: 关";
                m_messageTime = SDL_GetTicks();
                initButtons();
                if (m_engine.isThinking() && !isEngineTurn() && !m_analysisMode) {
                    m_ignoreNextBestMove = true;
                    m_engine.sendStop();
                    m_pvArrow1.valid = false;
                    m_pvArrow2.valid = false;
                    m_engineInfoText.clear();
                } else {
                    tryStartEngineMove();
                }
                break;
            case 6: // 设置
                if (m_uiState == UIState::SETTINGS) {
                    m_uiState = UIState::NORMAL;
                } else {
                    m_uiState = UIState::SETTINGS;
                    m_settingsScroll = 0;
                    m_settingsLines.clear();
                    m_settingsLines.push_back("引擎: " + m_settings.enginePath);
                    m_settingsLines.push_back("权重: " + m_settings.nnuePath);
                    m_settingsLines.push_back("搜索深度: " + std::to_string(m_settings.searchDepth));
                    m_settingsLines.push_back("搜索时间: " + std::to_string(m_settings.searchTime) + "ms");
                    m_settingsLines.push_back("线程数: " + std::to_string(m_settings.threads));
                    m_settingsLines.push_back("哈希表: " + std::to_string(m_settings.hashSize) + "MB");
                    m_settingsLines.push_back("云库: " + std::string(m_settings.useCloudBook ? "开启" : "关闭"));
                    m_settingsLines.push_back("引擎执红: " + std::string(m_settings.enginePlaysRed ? "开" : "关"));
                    m_settingsLines.push_back("引擎执黑: " + std::string(m_settings.enginePlaysBlack ? "开" : "关"));
                    m_settingsLines.push_back("");
                    m_settingsLines.push_back("快捷键:");
                    m_settingsLines.push_back("  N - 新对局");
                    m_settingsLines.push_back("  U - 悔棋");
                    m_settingsLines.push_back("  F - 翻转棋盘");
                    m_settingsLines.push_back("  M - 镜像棋盘");
                    m_settingsLines.push_back("  S - 停止引擎思考");
                    m_settingsLines.push_back("  A - 开关分析模式");
                    m_settingsLines.push_back("  C - 开关云库");
                    m_settingsLines.push_back("  ESC - 关闭设置");
                    m_settingsLines.push_back("  1-9 - 设置搜索深度(x2)");
                    m_settingsLines.push_back("  +/- - 调整线程数");
                    m_settingsLines.push_back("");

                    // 显示引擎选项
                    auto& opts = m_engine.getOptions();
                    if (!opts.empty()) {
                        m_settingsLines.push_back("引擎选项:");
                        for (auto& opt : opts) {
                            std::string line = "  " + opt.name + " (" + opt.type + ")";
                            if (!opt.value.empty()) line += " = " + opt.value;
                            if (opt.type == "spin") {
                                line += " [" + std::to_string(opt.min) + "-" + std::to_string(opt.max) + "]";
                            }
                            m_settingsLines.push_back(line);
                        }
                    }
                }
                break;
            case 7: // 云库
                m_settings.useCloudBook = !m_settings.useCloudBook;
                m_cloudBook.setEnabled(m_settings.useCloudBook);
                initButtons(); // 刷新按钮文字
                if (m_settings.useCloudBook) {
                    queryCloudBook();
                    m_message = "云库已开启";
                } else {
                    m_message = "云库已关闭";
                    m_cloudMoves.clear();
                    m_cloudErrorText.clear();
                }
                m_messageTime = SDL_GetTicks();
                break;
            case 8: // 停止
                if (m_engine.isThinking()) {
                    m_engine.sendStop();
                    m_message = "已停止思考";
                    m_messageTime = SDL_GetTicks();
                }
                break;
            case 9: // 分析
                toggleAnalysisMode();
                break;
            case 10: // 加载引擎
                openEngineFileDialog();
                break;
            case 11: // 首步
                if (!m_moveHistory.empty()) navigateTo(0);
                break;
            case 12: // 后退
                navigateBackward();
                break;
            case 13: // 前进
                navigateForward();
                break;
            case 14: // 末步
                navigateTo(-1);
                break;
            case 15: // 导出棋谱
                openPGNExportDialog();
                break;
            case 16: // 导入棋谱
                openPGNImportDialog();
                break;
            case 17: // 编辑棋局
                if (m_uiState == UIState::EDITING) {
                    exitEditMode();
                } else {
                    enterEditMode();
                }
                break;
        }
        return;
    }

    // 编辑模式
    if (m_uiState == UIState::EDITING) {
        // 先检查编辑面板点击
        int hit = editPaletteHitTest(x, y);
        if (hit >= 0) {
            if (hit <= 13) {
                // 选择棋子
                Side s = (hit < 7) ? Side::RED : Side::BLACK;
                m_editPiece.type = kPaletteOrder[hit < 7 ? hit : hit - 7];
                m_editPiece.side = s;
                return;
            } else if (hit == 14) {
                m_editPiece = {};  // 橡皮擦
                return;
            } else if (hit == 15) {
                // 清空棋盘
                for (int r = 0; r < Board::ROWS; r++)
                    for (int c = 0; c < Board::COLS; c++)
                        m_board.set(r, c, Piece{});
                return;
            } else if (hit == 16) {
                // 还原初始局面
                m_board.reset();
                return;
            } else if (hit == 17) {
                exitEditMode();
                return;
            } else if (hit == 18) {
                m_editFirstSide = Side::RED;
                return;
            } else if (hit == 19) {
                m_editFirstSide = Side::BLACK;
                return;
            }
        }
        // 棋盘点击：放置棋子
        int row, col;
        if (m_renderer.screenToBoard(x, y, row, col, m_settings.flipBoard, m_settings.mirrorBoard)) {
            handleEditBoardClick(row, col, false);
        }
        return;
    }

    // 设置面板下不响应棋盘点击
    if (m_uiState == UIState::SETTINGS) {
        m_uiState = UIState::NORMAL;
        return;
    }

    // 引擎思考中不允许操作棋盘（分析模式除外）
    if (m_engine.isThinking() && !m_analysisMode) return;

    // 棋谱点击（浏览历史时也允许点击跳转）
    int moveIdx = moveListHitTest(x, y);
    if (moveIdx >= 0 && moveIdx < (int)m_moveHistory.size()) {
        navigateTo(moveIdx);
        return;
    }

    // 浏览历史时不允许操作棋盘
    if (isViewingHistory()) return;

    // 棋盘点击
    int row, col;
    if (!m_renderer.screenToBoard(x, y, row, col, m_settings.flipBoard, m_settings.mirrorBoard))
        return;

    Piece clicked = m_board.get(row, col);

    if (m_selRow >= 0 && m_selCol >= 0) {
        // 已选中棋子
        if (clicked.side == m_board.currentSide()) {
            // 换选
            m_selRow = row;
            m_selCol = col;
            m_legalMoves = m_board.getLegalMoves(row, col);
        } else if (m_board.isLegalMove(m_selRow, m_selCol, row, col)) {
            // 走棋
            makeHumanMove(m_selRow, m_selCol, row, col);
        } else {
            // 非法走法，清除选择
            m_selRow = m_selCol = -1;
            m_legalMoves.clear();
        }
    } else {
        // 未选中，选子
        if (!clicked.empty() && clicked.side == m_board.currentSide()) {
            // 人机模式下不能走引擎控制的棋（分析模式除外）
            if (m_gameMode == GameMode::HUMAN_VS_ENGINE && !m_analysisMode && isEngineTurn())
                return;
            m_selRow = row;
            m_selCol = col;
            m_legalMoves = m_board.getLegalMoves(row, col);
        }
    }
}

void GUI::handleMouseUp(int, int) {}

void GUI::handleMouseMove(int x, int y) {
    updateButtons(x, y);
}

void GUI::handleKeyDown(SDL_Keycode key) {
    switch (key) {
        case SDLK_F11: {
            SDL_Window* win = m_renderer.window();
            Uint32 flags = SDL_GetWindowFlags(win);
            if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                SDL_SetWindowFullscreen(win, 0);
            else
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        case SDLK_n: newGame(); break;
        case SDLK_u: undoLastMove(); break;
        case SDLK_f: m_settings.flipBoard = !m_settings.flipBoard; break;
        case SDLK_m: m_settings.mirrorBoard = !m_settings.mirrorBoard; break;
        case SDLK_s:
            if (m_engine.isThinking()) m_engine.sendStop();
            break;
        case SDLK_a:
            toggleAnalysisMode();
            break;
        case SDLK_c:
            m_settings.useCloudBook = !m_settings.useCloudBook;
            m_cloudBook.setEnabled(m_settings.useCloudBook);
            break;
        case SDLK_ESCAPE:
            if (m_uiState == UIState::SETTINGS || m_uiState == UIState::FILE_BROWSER)
                m_uiState = UIState::NORMAL;
            else if (m_viewIndex >= 0)
                m_viewIndex = -1; // ESC退出浏览模式
            break;
        case SDLK_LEFT:
            navigateBackward();
            break;
        case SDLK_RIGHT:
            navigateForward();
            break;
        case SDLK_HOME:
            if (!m_moveHistory.empty()) navigateTo(0);
            break;
        case SDLK_END:
            navigateTo(-1);
            break;
        case SDLK_PLUS: case SDLK_EQUALS:
            m_settings.threads = std::min(m_settings.threads + 1, 64);
            if (m_engine.isRunning())
                m_engine.sendSetOption("Threads", std::to_string(m_settings.threads));
            m_message = "线程数: " + std::to_string(m_settings.threads);
            m_messageTime = SDL_GetTicks();
            break;
        case SDLK_MINUS:
            m_settings.threads = std::max(m_settings.threads - 1, 1);
            if (m_engine.isRunning())
                m_engine.sendSetOption("Threads", std::to_string(m_settings.threads));
            m_message = "线程数: " + std::to_string(m_settings.threads);
            m_messageTime = SDL_GetTicks();
            break;
        default:
            if (key >= SDLK_1 && key <= SDLK_9) {
                m_settings.searchDepth = (key - SDLK_1 + 1) * 2;
                m_message = "搜索深度: " + std::to_string(m_settings.searchDepth);
                m_messageTime = SDL_GetTicks();
            }
            break;
    }
}

void GUI::handleEvent(SDL_Event& ev) {
    switch (ev.type) {
        case SDL_QUIT:
            m_running = false;
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                handleResize(ev.window.data1, ev.window.data2);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT)
                handleMouseDown(ev.button.x, ev.button.y);
            else if (ev.button.button == SDL_BUTTON_RIGHT && m_uiState == UIState::EDITING) {
                // 右键：删除棋子
                int row, col;
                if (m_renderer.screenToBoard(ev.button.x, ev.button.y, row, col,
                                              m_settings.flipBoard, m_settings.mirrorBoard)) {
                    handleEditBoardClick(row, col, true);
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            handleMouseUp(ev.button.x, ev.button.y);
            break;
        case SDL_MOUSEMOTION:
            handleMouseMove(ev.motion.x, ev.motion.y);
            break;
        case SDL_KEYDOWN:
            handleKeyDown(ev.key.keysym.sym);
            break;
        case SDL_MOUSEWHEEL:
            if (m_uiState == UIState::FILE_BROWSER) {
                fbHandleScroll(ev.wheel.y);
            } else if (m_uiState == UIState::SETTINGS) {
                m_settingsScroll -= ev.wheel.y * 20;
                m_settingsScroll = std::max(0, m_settingsScroll);
            } else {
                // 检查鼠标是否在面板区域（棋谱列表）
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                if (mx >= m_renderer.panelLeft()) {
                    moveListHandleScroll(ev.wheel.y);
                }
            }
            break;
        case SDL_USEREVENT:
            if (ev.user.code == 1 && ev.user.data1) {
                std::string* moveStr = (std::string*)ev.user.data1;
                onEngineBestMove(*moveStr);
                delete moveStr;
            } else if (ev.user.code == 2 && ev.user.data1) {
                EngineInfo* info = (EngineInfo*)ev.user.data1;
                onEngineInfo(*info);
                delete info;
            } else if (ev.user.code == 3 && ev.user.data1) {
                auto* cbd = (CloudBookEventData*)ev.user.data1;
                onCloudBookResult(cbd->moves, cbd->error);
                delete cbd;
            }
            break;
    }
}
