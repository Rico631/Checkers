#pragma once
#include <chrono>
#include <thread>

#include "../Models/Project_path.h"
#include "Board.h"
#include "Config.h"
#include "Hand.h"
#include "Logic.h"

class Game
{
  public:
    Game() : board(config("WindowSize", "Width"), config("WindowSize", "Hight")), hand(&board), logic(&board, &config)
    {
        ofstream fout(project_path + "log.txt", ios_base::trunc);
        fout.close();
    }

    // to start checkers
    int play()
    {
        // Фиксируем начало игры
        auto start = chrono::steady_clock::now();

        if (is_replay) // Если перезапуск
        {
            // обновляем логику
            logic = Logic(&board, &config);
            // обновляем конфиг
            config.reload();
            // сбрасываем доску
            board.redraw();
        }
        else // иначе сбрасываем доску
        {
            board.start_draw();
        }
        is_replay = false;

        int turn_num = -1;
        bool is_quit = false;
        const int Max_turns = config("Game", "MaxNumTurns");
        // Шаги ограниченичены максимумом
        while (++turn_num < Max_turns)
        {
            beat_series = 0;
            // Проверка возможных ходов
            logic.find_turns(turn_num % 2);
            // Если ходов нет, то завершаем цикл
            if (logic.turns.empty())
                break;
            // Для бота (белого или черного) получаем из конфига глубину поиска
            logic.Max_depth = config("Bot", string((turn_num % 2) ? "Black" : "White") + string("BotLevel"));
            // Проерка что ходит человек
            if (!config("Bot", string("Is") + string((turn_num % 2) ? "Black" : "White") + string("Bot")))
            {
                // Ход
                auto resp = player_turn(turn_num % 2);
                if (resp == Response::QUIT) // Выход 
                {
                    is_quit = true;
                    break;
                }
                else if (resp == Response::REPLAY) // Перезапуск
                {
                    is_replay = true;
                    break;
                }
                else if (resp == Response::BACK) // Отмена хода
                {
                    // Проверяем выполнил предыдущий ход, что нет серии и ходов больше 2
                    if (config("Bot", string("Is") + string((1 - turn_num % 2) ? "Black" : "White") + string("Bot")) &&
                        !beat_series && board.history_mtx.size() > 2)
                    {
                        // Откат доски
                        board.rollback();
                        --turn_num;
                    }
                    // Проверка на серию
                    if (!beat_series)
                        --turn_num;
                    // Откат доски
                    board.rollback();
                    --turn_num;
                    // Сбрасываем серию
                    beat_series = 0;
                }
            }
            else
                bot_turn(turn_num % 2); // Ходит бот
        }
        // Время окончания игры
        auto end = chrono::steady_clock::now();
        ofstream fout(project_path + "log.txt", ios_base::app);
        // Записываем время игры
        fout << "Game time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();

        if (is_replay)
            return play();
        if (is_quit)
            return 0;
        int res = 2;
        if (turn_num == Max_turns)
        {
            res = 0;
        }
        else if (turn_num % 2)
        {
            res = 1;
        }
        // Отображаем финальную картинку
        board.show_final(res);
        // Ждем действия пользователя
        auto resp = hand.wait();
        // Нажал кнопуку перезапуск
        if (resp == Response::REPLAY)
        {
            is_replay = true;
            return play(); // Перезапускаем игру
        }
        return res;
    }

  private:
    void bot_turn(const bool color)
    {
        // Время начала хода
        auto start = chrono::steady_clock::now();
        
        auto delay_ms = config("Bot", "BotDelayMS");
        // new thread for equal delay for each turn
        thread th(SDL_Delay, delay_ms);
        // Ищем возможные ходы
        auto turns = logic.find_best_turns(color);
        // Ждем задержку
        th.join();
        bool is_first = true;
        // making moves
        for (auto turn : turns)
        {
            // Если не первй ход
            if (!is_first)
            {
                // Добавляем задержку
                SDL_Delay(delay_ms);
            }
            is_first = false;
            // Добавляем серию если за ход съедается фигура
            beat_series += (turn.xb != -1);
            // Перемещаем фигуру
            board.move_piece(turn, beat_series);
        }
        // Время завершения хода
        auto end = chrono::steady_clock::now();
        // Логируем ход
        ofstream fout(project_path + "log.txt", ios_base::app);
        fout << "Bot turn time: " << (int)chrono::duration<double, milli>(end - start).count() << " millisec\n";
        fout.close();
    }

    Response player_turn(const bool color)
    {
        // return 1 if quit
        vector<pair<POS_T, POS_T>> cells;
        for (auto turn : logic.turns)
        {
            cells.emplace_back(turn.x, turn.y);
        }
        // Подсветка всех доступных клеток для хода
        board.highlight_cells(cells);
        // Инициализация текущих координат
        move_pos pos = {-1, -1, -1, -1};
        POS_T x = -1, y = -1;
        // trying to make first move
        while (true)
        {
            // координаты выбранной клетки
            auto resp = hand.get_cell();
            // Возвращаем ответ если это не клетка
            if (get<0>(resp) != Response::CELL)
                return get<0>(resp);
            // Координаты клетки
            pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

            bool is_correct = false;
            // Проверка является ли клетка доступная для хода
            for (auto turn : logic.turns)
            {
                if (turn.x == cell.first && turn.y == cell.second)
                {
                    is_correct = true;
                    break;
                }
                // Если выбрал фигуру и конечную клетку хода
                if (turn == move_pos{x, y, cell.first, cell.second})
                {
                    pos = turn;
                    break;
                }
            }
            // Если найден правильный ход, выходим
            if (pos.x != -1)
                break;
            // Если не правильный, то сбрасываем
            if (!is_correct)
            {
                if (x != -1)
                {
                    board.clear_active();
                    board.clear_highlight();
                    board.highlight_cells(cells);
                }
                x = -1;
                y = -1;
                continue;
            }
            x = cell.first;
            y = cell.second;
            // Очищаем выделения
            board.clear_highlight();
            // Выделяем текущую клетку как активную
            board.set_active(x, y);
            // Собираем клетки для хода
            vector<pair<POS_T, POS_T>> cells2;
            for (auto turn : logic.turns)
            {
                if (turn.x == x && turn.y == y)
                {
                    cells2.emplace_back(turn.x2, turn.y2);
                }
            }
            // Подсвечиваем клетки
            board.highlight_cells(cells2);
        }

        // Очищаем подсветку
        board.clear_highlight();
        board.clear_active();
        // Перемещаем шашку
        board.move_piece(pos, pos.xb != -1);
        // Если нет съедаемых фигур, то возвращаем ОК
        if (pos.xb == -1)
            return Response::OK;
        // continue beating while can
        beat_series = 1;
        while (true)
        {
            // Находим съедаемые фигуры
            logic.find_turns(pos.x2, pos.y2);
            if (!logic.have_beats)
                break;

            // Собираем список клеток для дальнейшего хода
            vector<pair<POS_T, POS_T>> cells;
            for (auto turn : logic.turns)
            {
                cells.emplace_back(turn.x2, turn.y2);
            }
            // Подсвечиваем
            board.highlight_cells(cells);
            board.set_active(pos.x2, pos.y2);
            // trying to make move
            while (true)
            {
                // Действие пользователя
                auto resp = hand.get_cell();
                // Выход
                if (get<0>(resp) != Response::CELL)
                    return get<0>(resp);
                // Получаем координаты выбранной клетки
                pair<POS_T, POS_T> cell{get<1>(resp), get<2>(resp)};

                bool is_correct = false;
                // Проверяем выбранную клетку на корректность
                for (auto turn : logic.turns)
                {
                    if (turn.x2 == cell.first && turn.y2 == cell.second)
                    {
                        is_correct = true;
                        pos = turn;
                        break;
                    }
                }
                // Если не корректная клетка, повторяем процесс
                if (!is_correct)
                    continue;
                // Очистка подсветок
                board.clear_highlight();
                board.clear_active();
                // Увеличиваем серию
                beat_series += 1;
                // Перемещаем фигуру
                board.move_piece(pos, beat_series);
                break;
            }
        }
        // ОК
        return Response::OK;
    }

  private:
    Config config;
    Board board;
    Hand hand;
    Logic logic;
    int beat_series;
    bool is_replay = false;
};
