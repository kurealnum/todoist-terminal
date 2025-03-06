# todoist-terminal

Proof of concept, not designed for practical application. Every feature of todoist is _not_ replicated.

## Compiling and setup

Requirements:

- gcc
- libcurl
- ncurses
- libuuid

Setup and compile:

- Run `. compile.sh`
- Create a `.env` file in the directory that you compiled the project in
- Place your API token inside the `.env` file like so:

```
export TODOIST_AUTH_TOKEN="mytokenhere"
```

- Refer to [Todoist's documentation](https://developer.todoist.com/guides/#our-apis) for how to acquire an API token
- Run the compiled file

## Current functionality

All currently supported functionality in the form of keybinds (aspiring to be somewhat vim-like).

### In the projects menu (initial menu)

- `q` - exit program
- `j` - move down an item
- `k` - move up an item
- `l` - open the currently selected project

### In the tasks menu

- `q` - exit program
- `j` - move down an item
- `k` - move up an item
- `p` - close the currently selected task
- `o` - reopen the currently selected task
- `i` - create a new task (input characters, press `enter` to submit, press `q` to cancel)
- `d` - delete a task (will ask for confirmation, press `y` to accept)
