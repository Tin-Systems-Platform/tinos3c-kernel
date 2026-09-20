#include <ncurses.h>
#include <stdlib.h>  // For system() function
#include <string.h>  // For strlen() function

#define NUM_TASKS 3

// List of tasks to choose from
const char *tasks[NUM_TASKS] = {
    "1. Build OS",
    "2. Run OS",
    "3. Exit"
};

// Corresponding commands for each task
const char *commands[NUM_TASKS] = {
    "make build",
    "make run",  // Run the OS
    ""  // Exit (no command needed)
};

// Function to display the menu
void display_menu(WINDOW *win, int highlight) {
    int x, y;
    int i;

    getmaxyx(win, y, x);  // Get window size

    // Title
    char *title = "Tinos3c kernel: Build and Compile tool";
    mvwprintw(win, 0, (x - strlen(title)) / 2, "%s", title);

    // Print tasks
    for(i = 0; i < NUM_TASKS; i++) {
        if(i == highlight) {
            wattron(win, A_REVERSE);  // Highlight the current task
            mvwprintw(win, i + 2, (x - strlen(tasks[i])) / 2, "%s", tasks[i]);
            wattroff(win, A_REVERSE); // Remove highlight
        } else {
            mvwprintw(win, i + 2, (x - strlen(tasks[i])) / 2, "%s", tasks[i]);
        }
    }

    wrefresh(win);
}

// Function to run the selected task
void run_task(int task_index) {
    if (task_index >= NUM_TASKS || task_index < 0) {
        return;  // Avoid out-of-bounds access
    }

    if (task_index == NUM_TASKS - 1) {
        // Exit option (no command needed)
        return;
    }

    // Run the corresponding command for the selected task
    int result = system(commands[task_index]);

    if (result == -1) {
        perror("Error executing command");
    }
}

// Main function to run the TUI
int main() {
    int ch;
    int highlight = 0;

    // Initialize ncurses
    initscr();
    cbreak();               // Disable line buffering
    noecho();               // Don't echo input
    curs_set(0);            // Hide cursor
    start_color();          // Enable color functionality
    init_pair(1, COLOR_BLACK, COLOR_WHITE); // Define color pair for highlighting

    // Create a window
    WINDOW *win = newwin(10, 50, 5, 5);  // 10 rows, 50 columns, at position (5,5)
    keypad(win, TRUE);  // Enable special keys (e.g., arrow keys)

    while(1) {
        display_menu(win, highlight);
        ch = wgetch(win);

        switch(ch) {
            case KEY_UP:
                if(highlight > 0)
                    highlight--;
                break;
            case KEY_DOWN:
                if(highlight < NUM_TASKS - 1)
                    highlight++;
                break;
            case 10:  // Enter key
                run_task(highlight);  // Run the selected task
                if(highlight == NUM_TASKS - 1) {
                    endwin();
                    return 0; // Exit program
                } else {
                    // For now, display a simple message after the task is run
                    clear();
                    mvprintw(0, 0, "Task '%s' completed.", tasks[highlight]);
                    mvprintw(1, 0, "Press any key to return to the menu...");
                    refresh();
                    getch();
                }
                break;
            case 27:  // Escape key to quit
                endwin();
                return 0;
        }
    }

    // Clean up ncurses
    endwin();
    return 0;
}