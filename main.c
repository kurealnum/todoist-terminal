// Ncurses work is at the very least partial courtesy of Pradeep Padala:
// https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/index.html

#include <cdk.h>
#include <cdk/dialog.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curses.h>
#include <form.h>
#include <menu.h>
#include <ncurses.h>
#include <panel.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>

#define BASE_REST_URL "https://api.todoist.com/rest/v2/"
#define BASE_SYNC_URL "https://api.todoist.com/sync/v9/sync"
#define NO_TASKS_TO_COMPLETE_MESSAGE                                           \
  "No tasks left to complete! Have a good day!"

// Structs
//
struct memory {
  char *response;
  size_t size;
};

struct menuTask {
  char *content;
};

// Url args can simply be added to the url itself
struct curlArgs {
  CURL *curl;
  struct curl_slist *headers;
  char *method;
  char *url;
  char *postFields;
};

struct taskMetaData {
  char *id;
  char *content;
  int priority;
};
//
// End structs

// Headers
//

// Helper function for combining two strings. Return value needs to be
// free()-ed.
char *combineString(char *str1, char *str2);

// Helper function for writing with curl
static size_t curlWriteHelper(char *data, size_t size, size_t nmemb,
                              void *clientp);

// Displays a message to the user and awaits a keypress before remove itself.
// Clears the ncurses window
void displayMessage(char *message);

// Helper function for making a request. Return value needs to be free()-ed
cJSON *makeRequest(struct curlArgs curlArgs);

// Returns a menu. Must be free()-ed
MENU *renderMenuFromJson(cJSON *json, char *query);

// Function for rendering a certain project's tasks. Check out Todoist itself
// for a little bit more insight on how this is set up.
void projectPanel(struct curlArgs curlArgs, int row, int col);

// Helper function. Given a menu and a cJSON array, it returns the currently
// selected item as a cJSON struct. The cJSON array and menu items need to be
// the same length and in the same order
cJSON *getCurrentItemJson(MENU *menu, cJSON *json);

// Closes a task, and returns the updated list of items
ITEM **closeTask(cJSON *tasksJson, MENU *tasksMenu, struct curlArgs curlArgs);

// Helper function. See source.
void setItemsAndRepostMenu(MENU *menu, ITEM **items);

// Helper function to get the value from a JSON object
char *getJsonValue(cJSON *json, char *key);

// Similar logic to closeTask. We don't have to return anything, because there's
// no UI changes to make.
boolean reopenTask(cJSON *tasksJson, MENU *tasksMenu, struct curlArgs curlArgs);

// Creates a cJSON item that looks like this:
// https://developer.todoist.com/sync/v9/#due-dates
cJSON *createJsonDueCommand(char *string, char *itemId);

// Displays an input field to the user (clears screen). Returns the char *
// (needs to be free()-ed) to what the user inputted.
char *displayInputField(char *infoText);

// Sort array. For those of you who are algorithmically inclined, I am so, so,
// sorry that you have to see this. This is awful. It's kinda hard to
// sort in any other way, because cJSON items are basically linked lists.
cJSON *sortTasks(cJSON *json);

// Creates a new task, returns updated array of items
ITEM **createTask(struct curlArgs curlArgs, cJSON *tasksJson);

// Creates new items from JSON. Needs to be free()-ed and to have an NULL
// appended to the end of the return value.
ITEM **createItemsFromJson(cJSON *json, int customLength, char *query);

// Very similar to getJsonValue, but returns the valueint instead.
int getJsonIntValue(cJSON *json, char *key);

// Deletes a task, returns list of new items if successfull, and null if it
// isn't.
ITEM **deleteTask(cJSON *tasksJson, struct curlArgs curlArgs, MENU *curMenu);
//
// End Headers

int main(void) {
  // ncurses. stdscr acts as the "background", and everything else sits on top
  // of it.
  initscr();
  raw();
  noecho();
  printw("Loading current projects. Press q to exit.\n");
  refresh();

  int row, col;
  getmaxyx(stdscr, row, col);
  keypad(stdscr, TRUE);

  // curl
  CURL *curl = curl_easy_init();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  if (!curl) {
    printf("curl didn't initalize correctly.\n");
    return 1;
  } else {
    // Get auth token from environment
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      displayMessage("Unable to find auth token. Press any button to end the "
                     "program.\n");
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      endwin();
      return 1;
    }

    char *authHeader = combineString("Authorization: Bearer ", authToken);
    struct curl_slist *baseHeaders = NULL;
    baseHeaders = curl_slist_append(baseHeaders, authHeader);

    // Get list of projects
    char *allProjectsUrl = combineString(BASE_REST_URL, "projects");
    struct curlArgs allProjectsCurlArgs = {curl, baseHeaders, "GET",
                                           allProjectsUrl};
    cJSON *projectsJson = makeRequest(allProjectsCurlArgs);
    int numOfProjects = cJSON_GetArraySize(projectsJson);

    // Adding a cJSON object so the user can also view active tasks (AKA the
    // today section)
    cJSON *today = cJSON_CreateObject();
    if (today == NULL) {
      goto end;
    }
    if (cJSON_AddStringToObject(today, "name", "Today") == NULL) {
      goto end;
    }

    // Adding a "fake" ID field to help with managing menu (see event loop ~20
    // lines down)
    if (cJSON_AddStringToObject(today, "id", "999") == NULL) {
      goto end;
    }
    cJSON_AddItemToArray(projectsJson, today);

    MENU *projectsMenu = renderMenuFromJson(projectsJson, "name");
    if (projectsMenu == NULL) {
      goto end;
    }

    // Render
    clear();
    set_menu_mark(projectsMenu, NULL);
    post_menu(projectsMenu);
    refresh();

    // For free()-ing
    ITEM **projectsItems = menu_items(projectsMenu);

    int getchChar;
    while ((getchChar = getch()) != 'q') {
      if (getchChar == 'j') {
        menu_driver(projectsMenu, REQ_DOWN_ITEM);
      } else if (KEY_UP || getchChar == 'k') {
        menu_driver(projectsMenu, REQ_UP_ITEM);
      } else if (getchChar == 'q') {
        break;
      } else if (getchChar == 'l') {
        // Find project ID, and call projectPanel with that project ID in a
        // curlArgs struct
        cJSON *currentItemJson = getCurrentItemJson(projectsMenu, projectsJson);
        if (currentItemJson == NULL) {
          displayMessage(
              "Something went wrong when trying to access the current "
              "item of the Ncurses menu. Press any key to quit.");
          goto end;
        }
        cJSON *projectIDJson =
            cJSON_GetObjectItemCaseSensitive(currentItemJson, "id");
        if (projectIDJson == NULL) {
          displayMessage("JSON for project ID is null. Press any key to quit.");
          goto end;
        }

        char *projectID = projectIDJson->valuestring;
        char *tasksUrl;
        if (strcmp(projectID, "999") == 0) {
          tasksUrl = combineString(BASE_REST_URL, "tasks/?filter=today");
        } else {
          tasksUrl = combineString(BASE_REST_URL, "tasks/?project_id=");
          tasksUrl = combineString(tasksUrl, projectID);
        }

        struct curlArgs projectPanelCurlArgs = {curl, baseHeaders, "GET",
                                                tasksUrl};
        projectPanel(projectPanelCurlArgs, row, col);

        // Scuffed fix for projects list not loading after exiting from
        // project panel
        menu_driver(projectsMenu, REQ_NEXT_ITEM);
        menu_driver(projectsMenu, REQ_PREV_ITEM);

        // Once projectPanel returns, the user has exited the panel.
        free(tasksUrl);
      }
    }

  end:
    // Cleanup and free variables
    curl_easy_cleanup(curl);
    free(authHeader);
    free(projectsMenu);
    free(projectsJson);
    for (int i = 0; i < numOfProjects; i++) {
      free(projectsItems[i]);
    }
    endwin();
  }
  curl_global_cleanup();
}

char *displayInputField(char *infoText) {
  // Fields
  const int lineLength = 50;
  FIELD *input[2];
  input[0] = new_field(1, lineLength, 0, 0, 0, 0);
  set_field_back(input[0], A_UNDERLINE);
  input[1] = NULL;

  // Forms
  FORM *form = new_form(input);

  // Render
  clear();
  post_form(form);
  refresh();

  // Event loop
  int getchChar = 0;
  char *res = malloc(lineLength + 1);

  if (!res) {
    return NULL;
  }

  int i = 0;
  while ((getchChar = getch()) != 'q') {
    if (getchChar == KEY_BACKSPACE) {
      form_driver(form, REQ_DEL_PREV);
      res[i] = '\0';
      i--;
    } else if (getchChar == KEY_ENTER || getchChar == 10) {
      form_driver(form, REQ_VALIDATION);
      break;
    } else {
      form_driver(form, getchChar);
      // Yes, this isn't how this should be done. It's the best way though.
      res[i] = getchChar;
      i++;
    }
    refresh();
  }

  res[i] = '\0';

  unpost_form(form);
  free_form(form);
  free_field(input[0]);
  return res;
}

char *combineString(char *str1, char *str2) {
  char *newString = malloc((strlen(str1) + strlen(str2) + 1) * sizeof(char));
  if (newString == NULL) {
    return NULL;
  }
  strcpy(newString, str1);
  strcat(newString, str2);
  return newString;
}

static size_t curlWriteHelper(char *data, size_t size, size_t nmemb,
                              void *clientp) {
  size_t realsize = size * nmemb;
  struct memory *mem = (struct memory *)clientp;

  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if (!ptr) {
    printf("Realloc ran out of memory\n");
    return 0;
  }

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

void displayMessage(char *message) {
  clear();
  printw("%s", message);
  refresh();
  getch();
  clear();
}

cJSON *makeRequest(struct curlArgs curlArgs) {
  struct memory requestData;
  cJSON *requestsJson;

  requestData.response = malloc(1);
  requestData.size = 0;

  CURL *curl = curlArgs.curl;

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteHelper);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlArgs.headers);
  curl_easy_setopt(curl, CURLOPT_URL, curlArgs.url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&requestData);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, curlArgs.method);

  if (curlArgs.postFields != NULL) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, curlArgs.postFields);
  } else {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    displayMessage("curl_easy_perform() failed.");
  } else {
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode == 204) {
      cJSON *blank = cJSON_CreateArray();
      return blank;
    }

    requestsJson = cJSON_Parse(requestData.response);

    if (requestsJson == NULL) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
        // Can't use displayMessage because of `const char *`.
        clear();
        printw("%s", error_ptr);
        refresh();
        getch();
        clear();
      } else {
        displayMessage("Failed to parse JSON. Error could not be shown.");
      }
    } else {
      return requestsJson;
    }
  }

  return requestsJson;
}

MENU *renderMenuFromJson(cJSON *json, char *query) {
  cJSON *currentTask = NULL;
  int itemsLength = cJSON_GetArraySize(json);

  // QOL
  if (itemsLength == 0) {
    ITEM **blankItems = (ITEM **)malloc(2 * sizeof(struct ITEM *));
    blankItems[0] = new_item(NO_TASKS_TO_COMPLETE_MESSAGE, "");
    blankItems[1] = (ITEM *)NULL;
    MENU *blankMenu = new_menu(blankItems);
    return blankMenu;
  }

  ITEM **items = createItemsFromJson(json, itemsLength + 1, query);

  items[itemsLength] = (ITEM *)NULL;
  MENU *menu = new_menu(items);
  return menu;
}

cJSON *getCurrentItemJson(MENU *menu, cJSON *json) {
  ITEM *currentItem = current_item(menu);
  int currentItemIndex = item_index(currentItem);
  cJSON *currentItemJson = cJSON_GetArrayItem(json, currentItemIndex);
  return currentItemJson;
}

cJSON *sortTasks(cJSON *json) {
  int tasksLength = cJSON_GetArraySize(json);
  cJSON *tasksJson = cJSON_CreateArray();
  if (tasksJson == NULL) {
    return NULL;
  }

  // Todoist priorities come in the form: P1 = 4, P4 = 1. Why? Only a higher
  // power knows.
  // TODO
  cJSON *task = NULL;
  int target = 4;
  while (cJSON_GetArraySize(tasksJson) <= tasksLength && target >= 1) {
    cJSON_ArrayForEach(task, json) {
      // Get all the values that we're going to need
      cJSON *curPriority = cJSON_GetObjectItemCaseSensitive(task, "priority");
      if (curPriority == NULL) {
        return NULL;
      }
      if (!cJSON_IsNumber(curPriority)) {
        return NULL;
      }

      cJSON *curContent = cJSON_GetObjectItemCaseSensitive(task, "content");
      if (curContent == NULL) {
        return NULL;
      }
      if (!cJSON_IsString(curContent)) {
        return NULL;
      }

      cJSON *curId = cJSON_GetObjectItemCaseSensitive(task, "id");
      if (curId == NULL) {
        return NULL;
      }
      // curId is a inputted as a string for some reason
      if (!cJSON_IsString(curId)) {
        return NULL;
      }

      if (curPriority->valueint == target) {
        cJSON *newTask = cJSON_CreateObject();
        cJSON_AddItemToObject(newTask, "priority",
                              cJSON_CreateNumber(curPriority->valueint));
        cJSON_AddStringToObject(newTask, "content", curContent->valuestring);

        // The id is a string because why should we bother going to an int and
        // then back?
        cJSON_AddStringToObject(newTask, "id", curId->valuestring);
        cJSON_AddItemToArray(tasksJson, newTask);
      }
    }
    target -= 1;
  }

  return tasksJson;
}

void projectPanel(struct curlArgs curlArgs, int row, int col) {
  PANEL *projectPanel;
  WINDOW *projectWindow;

  // Query for list of currently open tasks
  char *tasksUrl = combineString(BASE_REST_URL, "tasks");
  cJSON *unsortedTasksJson = makeRequest(curlArgs);

  // Get menu
  cJSON *tasksJson = sortTasks(unsortedTasksJson);
  int tasksLength = cJSON_GetArraySize(tasksJson);
  MENU *tasksMenu = renderMenuFromJson(tasksJson, "content");
  int menuCol = 1;
  int *menuRow = &tasksLength;
  int res = set_menu_format(tasksMenu, *menuRow, menuCol);

  projectWindow = newwin(row, col, 0, 0);
  projectPanel = new_panel(projectWindow);
  set_menu_mark(tasksMenu, NULL);

  // Render
  clear();
  update_panels();
  post_menu(tasksMenu);
  wrefresh(projectWindow);
  refresh();

  // Event loop (ish?). Think of 'break' as going back to the projects menu.
  int getchChar;
  while ((getchChar = getch()) != 'q') {
    if (getchChar == KEY_DOWN || getchChar == 'j') {
      menu_driver(tasksMenu, REQ_DOWN_ITEM);
    } else if (getchChar == KEY_UP || getchChar == 'k') {
      menu_driver(tasksMenu, REQ_UP_ITEM);
    } else if (getchChar == 'h') {
      break;
    } else if (getchChar == 'p') {
      ITEM **newItems = closeTask(tasksJson, tasksMenu, curlArgs);
      if (newItems == NULL) {
        break;
      }
      setItemsAndRepostMenu(tasksMenu, newItems);
      refresh();
    } else if (getchChar == 'o') {
      if (!reopenTask(tasksJson, tasksMenu, curlArgs)) {
        break;
      };
      post_menu(tasksMenu);
      refresh();
    } else if (getchChar == 'i') {
      ITEM **newItems = createTask(curlArgs, tasksJson);
      if (!newItems) {
        displayMessage("Creating new task failed. Press any key to return to "
                       "the main menu.");
        return;
      }

      setItemsAndRepostMenu(tasksMenu, newItems);
      refresh();
    } else if (getchChar == 'd') {
      ITEM **newItems = deleteTask(tasksJson, curlArgs, tasksMenu);

      // If deleteTask returns NULL, it doesn't necesarrily mean that anything
      // failed. It just means that the user might've closed out of it.
      if (newItems) {
        setItemsAndRepostMenu(tasksMenu, newItems);
      }
      refresh();
    }
  }

  del_panel(projectPanel);
  unpost_menu(tasksMenu);
  update_panels();
  refresh();

  // Free variables and whatnot
  ITEM **taskItems = menu_items(tasksMenu);
  free(tasksUrl);
  free(tasksJson);
  for (int i = 0; i < tasksLength; i++) {
    free_item(taskItems[i]);
    struct taskMetaData *toFreeTMD =
        (struct taskMetaData *)item_userptr(taskItems[i]);
    free(toFreeTMD);
  }
  free_menu(tasksMenu);
}

ITEM **createTask(struct curlArgs curlArgs, cJSON *tasksJson) {
  char *newTaskName = displayInputField("Enter the name of a new task.");
  if (newTaskName == NULL) {
    displayMessage("There was an error saving the ncurses field.");
    return NULL;
  } else {
    // Create headers
    struct curl_slist *createTaskHeaders = NULL;
    createTaskHeaders =
        curl_slist_append(createTaskHeaders, curlArgs.headers->data);

    // Create Json
    cJSON *createTaskPostFieldsJson = cJSON_CreateArray();
    cJSON *newTask = cJSON_CreateObject();

    if (!cJSON_AddItemToArray(createTaskPostFieldsJson, newTask)) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    if (!cJSON_AddStringToObject(newTask, "type", "item_add")) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    // Create and add uuid
    uuid_t tmp_binuuid;
    uuid_generate_random(tmp_binuuid);
    char *tmp_uuid = malloc(37);
    uuid_unparse(tmp_binuuid, tmp_uuid);
    if (!cJSON_AddStringToObject(newTask, "temp_id", tmp_uuid)) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char *uuid = malloc(37);
    uuid_unparse(binuuid, uuid);
    if (!cJSON_AddStringToObject(newTask, "uuid", uuid)) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    cJSON *args = cJSON_CreateObject();
    if (!cJSON_AddItemToObject(newTask, "args", args)) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    if (!cJSON_AddStringToObject(args, "content", newTaskName)) {
      displayMessage("There was an error creating the JSON. Press any key "
                     "to return to the main menu.");
      return NULL;
    }

    char *commands = combineString(
        "commands=", cJSON_PrintUnformatted(createTaskPostFieldsJson));

    struct curlArgs createTaskCurlArgs = {curlArgs.curl, createTaskHeaders,
                                          "POST", BASE_SYNC_URL, commands};

    // Request
    cJSON *result = makeRequest(createTaskCurlArgs);
    free(commands);
    if (result == NULL) {
      displayMessage(
          "Something went wrong when making the request to close the "
          "task. Press any key to return to the projects menu.");
      return NULL;
    }
    // tmp_uuid will be free later on
    free(uuid);

    int curItemsLength = cJSON_GetArraySize(tasksJson);

    // Make space for new item (hence + 2)
    ITEM **newItems =
        createItemsFromJson(tasksJson, curItemsLength + 2, "content");

    newItems[curItemsLength] = new_item(newTaskName, "1");

    struct taskMetaData *newTaskMetaData =
        (struct taskMetaData *)malloc(sizeof(struct taskMetaData));
    newTaskMetaData->content = newTaskName;

    // Get and set new task's id from response
    cJSON *tmpIdMapping =
        cJSON_GetObjectItemCaseSensitive(result, "temp_id_mapping");

    if (!tmpIdMapping) {
      displayMessage("Something went wrong when accessing the id of the new "
                     "task. Press any key to return to the projects menu.");
      return NULL;
    }

    cJSON *idJson = cJSON_GetObjectItemCaseSensitive(tmpIdMapping, tmp_uuid);

    if (!idJson) {
      displayMessage("Something went wrong when accessing the id of the new "
                     "task. Press any key to return to the projects menu.");
      return NULL;
    }

    free(tmp_uuid);
    char *id = idJson->valuestring;

    newTaskMetaData->id = id;
    newTaskMetaData->priority = 1;
    set_item_userptr(newItems[curItemsLength], newTaskMetaData);

    newItems[curItemsLength + 1] = (ITEM *)NULL;

    return newItems;
  }
}

ITEM **createItemsFromJson(cJSON *json, int customLength, char *query) {
  ITEM **newItems = (ITEM **)malloc(customLength * sizeof(struct ITEM *));
  int itemsLength = cJSON_GetArraySize(json);

  for (int i = 0; i < itemsLength; i++) {
    cJSON *curItem = cJSON_GetArrayItem(json, i);
    if (curItem == NULL) {
      return NULL;
    }
    cJSON *curContent = cJSON_GetObjectItemCaseSensitive(curItem, query);
    if (curContent == NULL) {
      return NULL;
    }

    // If curPriority is NULL, we're rendering the projects menu
    cJSON *curPriority = cJSON_GetObjectItemCaseSensitive(curItem, "priority");

    if (curPriority == NULL) {
      newItems[i] = new_item(curContent->valuestring, NULL);
    } else {
      cJSON *curId = cJSON_GetObjectItemCaseSensitive(curItem, "id");
      if (!curId) {
        return NULL;
      }

      if (!cJSON_IsString(curId)) {
        return NULL;
      }

      // No idea why we need to do this for the priority field...
      newItems[i] = new_item(curContent->valuestring, cJSON_Print(curPriority));

      // Add metadata to userptr (DON'T FORGET TO FREE() THIS)
      struct taskMetaData *newTaskMetaData =
          (struct taskMetaData *)malloc(sizeof(struct taskMetaData *));
      newTaskMetaData->content = curContent->string;
      newTaskMetaData->priority = curPriority->valueint;
      newTaskMetaData->id = curId->valuestring;
      set_item_userptr(newItems[i], newTaskMetaData);
    }
  }

  return newItems;
}

void setItemsAndRepostMenu(MENU *menu, ITEM **items) {
  unpost_menu(menu);

  // Free old menu items
  ITEM **oldItems = menu_items(menu);
  int i = 0;
  while (oldItems[i] != NULL) {
    free(oldItems[i]);
    i++;
  }
  free(oldItems[i]);

  // Hopefully this doesn't shoot me in the foot later on
  int row, col;
  getmaxyx(stdscr, row, col);
  set_menu_items(menu, items);
  set_menu_format(menu, row, 1);
  post_menu(menu);
}

char *getJsonValue(cJSON *json, char *key) {
  cJSON *keyValuePair = cJSON_GetObjectItemCaseSensitive(json, key);
  if (keyValuePair == NULL) {
    displayMessage("Something went wrong when closing the task. Press any "
                   "key to return to the projects menu. (getJsonValue 1)");
    return NULL;
  }
  char *value = keyValuePair->valuestring;
  if (value == NULL) {
    displayMessage("Something went wrong when closing the task. Press any "
                   "key to return to the projects menu. (getJsonValue 2)");
    return NULL;
  }

  return value;
}

int getJsonIntValue(cJSON *json, char *key) {
  cJSON *keyValuePair = cJSON_GetObjectItemCaseSensitive(json, key);
  if (keyValuePair == NULL) {
    displayMessage("Something went wrong when closing the task. Press any "
                   "key to return to the projects menu. (getJsonIntValue 1)");
    return -1;
  }
  return keyValuePair->valueint;
}

cJSON *createJsonDueCommand(char *string, char *itemId) {
  cJSON *postFieldsJson = cJSON_CreateObject();
  cJSON *commands = cJSON_CreateArray();
  cJSON *command = cJSON_CreateObject();
  if (postFieldsJson == NULL || commands == NULL || command == NULL) {
    displayMessage(
        "Something went wrong when creating JSON for request (2). Press "
        "any key to return to the projects menu.");
    return false;
  }
  cJSON_AddItemToObject(postFieldsJson, "commands", commands);
  cJSON_AddItemToArray(commands, command);

  // Type, uuid, and args
  // uuid specifically
  uuid_t binuuid;
  uuid_generate_random(binuuid);
  char *uuid = malloc(37);
  uuid_unparse(binuuid, uuid);

  cJSON *args = cJSON_CreateObject();
  if (cJSON_AddStringToObject(command, "type", "item_update") == NULL) {
    displayMessage(
        "Something went wrong when creating JSON for request (3). Press "
        "any key to return to the projects menu.");
    return false;
  }
  if (cJSON_AddStringToObject(command, "uuid", uuid) == NULL) {
    displayMessage(
        "Something went wrong when creating JSON for request (4). Press "
        "any key to return to the projects menu.");
    return false;
  }
  // Why false and not NULL? No idea.
  if (cJSON_AddItemToObject(command, "args", args) == false) {
    displayMessage(
        "Something went wrong when creating JSON for request (5). Press "
        "any key to return to the projects menu.");
    return false;
  }

  // Id and due fields on args
  cJSON *due = cJSON_CreateObject();
  if (cJSON_AddStringToObject(args, "id", itemId) == NULL) {
    displayMessage(
        "Something went wrong when creating JSON for request (6). Press "
        "any key to return to the projects menu.");
    return false;
  }
  // This is where we actually set the due date to today, thus "reopening" the
  // task
  if (cJSON_AddStringToObject(due, "string", string) == NULL) {
    displayMessage(
        "Something went wrong when creating JSON for request (7). Press "
        "any key to return to the projects menu.");
    return false;
  }
  if (cJSON_AddItemToObject(args, "due", due) == false) {
    displayMessage(
        "Something went wrong when creating JSON for request (9). Press "
        "any key to return to the projects menu.");
    return false;
  }

  return postFieldsJson;
}

boolean reopenTask(cJSON *tasksJson, MENU *tasksMenu,
                   struct curlArgs curlArgs) {
  // Get information about the currently selected item
  ITEM *currentItem = current_item(tasksMenu);
  cJSON *currentItemJson = getCurrentItemJson(tasksMenu, tasksJson);
  char *currentItemId = getJsonValue(currentItemJson, "id");
  if (currentItemId == NULL) {
    displayMessage("Something went wrong when closing the task. Press any key "
                   "to return to the projects menu. (reopenTask 1)");
    return false;
  }
  char *reopenTaskUrl = BASE_SYNC_URL;

  // Gathering data for postFields (cJSON * -> char *)
  cJSON *postFieldsJson =
      createJsonDueCommand("every day starting today", currentItemId);

  if (postFieldsJson == NULL) {
    return false;
  }

  char *postFields = cJSON_PrintUnformatted(postFieldsJson);

  // Making the request
  struct curl_slist *reopenHeaders = NULL;
  reopenHeaders = curl_slist_append(reopenHeaders, curlArgs.headers->data);
  reopenHeaders =
      curl_slist_append(reopenHeaders, "Content-Type: application/json");
  reopenHeaders = curl_slist_append(reopenHeaders, "Accept: application/json");
  struct curlArgs reopenTaskArgs = {curlArgs.curl, reopenHeaders, "POST",
                                    reopenTaskUrl, postFields};
  cJSON *result = makeRequest(reopenTaskArgs);

  if (result == NULL) {
    displayMessage("Something went wrong when making the request to close the "
                   "task. Press any key to return to the projects menu.");
    return false;
  }

  return true;
}

ITEM **closeTask(cJSON *tasksJson, MENU *tasksMenu, struct curlArgs curlArgs) {
  ITEM *currentItem = current_item(tasksMenu);

  // Possibly hacky solution -- if there are no items to complete, just return
  if (strcmp(item_name(currentItem), NO_TASKS_TO_COMPLETE_MESSAGE) == 0) {
    return menu_items(tasksMenu);
  }

  cJSON *currentItemJson = getCurrentItemJson(tasksMenu, tasksJson);
  if (currentItemJson == NULL) {
    displayMessage("Something went wrong when closing the task. Press any "
                   "key to return to the projects menu (closeTask 1).");
    return NULL;
  }

  char *currentItemId = getJsonValue(currentItemJson, "id");

  cJSON *postFieldsJson =
      createJsonDueCommand("every day starting tomorrow", currentItemId);

  if (postFieldsJson == NULL) {
    return NULL;
  }

  char *postFields = cJSON_PrintUnformatted(postFieldsJson);

  // Headers
  char *closeTaskUrl = BASE_SYNC_URL;
  struct curl_slist *markCompleteHeaders = NULL;
  markCompleteHeaders =
      curl_slist_append(markCompleteHeaders, curlArgs.headers->data);
  markCompleteHeaders =
      curl_slist_append(markCompleteHeaders, "Content-Type: application/json");
  markCompleteHeaders =
      curl_slist_append(markCompleteHeaders, "Accept: application/json");
  struct curlArgs markCompleteArgs = {curlArgs.curl, markCompleteHeaders,
                                      "POST", closeTaskUrl, postFields};

  // Make request
  cJSON *markCompleteJson = makeRequest(markCompleteArgs);
  if (markCompleteJson == NULL) {
    displayMessage("Something went wrong when making an API request to close "
                   "the task. Press any key to return to the projects menu.");
    return NULL;
  }

  // If it isn't null, the request was successfull, so we can update the
  // menu.
  // Free old items first
  int tasksLength = cJSON_GetArraySize(tasksJson);

  // If there's not going to be anything in the ITEM ** return anyways, don't
  // bother doing anything. Also helps avoid an annoying bug where, when
  // completing the last item of the list, the user will be left with random
  // characters instead of a blank menu.
  if (tasksLength - 1 == 0) {
    ITEM **blankItems = (ITEM **)malloc(2 * sizeof(struct ITEM *));
    blankItems[0] = new_item(NO_TASKS_TO_COMPLETE_MESSAGE, "");
    blankItems[1] = (ITEM *)NULL;
    return blankItems;
  }

  ITEM **taskItems = menu_items(tasksMenu);
  for (int i = 0; i < tasksLength; i++) {
    free_item(taskItems[i]);
    struct taskMetaData *toFreeTMD =
        (struct taskMetaData *)item_userptr(taskItems[i]);
    free(toFreeTMD);
  }

  // Delete completed task
  int completedTaskIndex = item_index(currentItem);
  cJSON_DeleteItemFromArray(tasksJson, completedTaskIndex);

  // Create new items (will need to be free()-ed)
  int newItemsLength = cJSON_GetArraySize(tasksJson);
  ITEM **newItems =
      createItemsFromJson(tasksJson, newItemsLength + 1, "content");
  newItems[newItemsLength] = (ITEM *)NULL;

  return newItems;
}

ITEM **deleteTask(cJSON *tasksJson, struct curlArgs curlArgs, MENU *curMenu) {
  clear();
  printw("Are you sure you want to delete this task?");

  int getchChar = getch();
  if (getchChar == 'y') {

    struct taskMetaData *currentTaskMetaData =
        (struct taskMetaData *)item_userptr(current_item(curMenu));

    if (!currentTaskMetaData) {
      return NULL;
    }

    char *currentItemId = currentTaskMetaData->id;

    if (!currentItemId) {
      return NULL;
    }

    char *url =
        combineString(BASE_REST_URL, combineString("tasks/", currentItemId));

    // Assemble request args
    struct curlArgs deleteTaskCurlArgs = {curlArgs.curl, curlArgs.headers,
                                          "DELETE", url, curlArgs.postFields};

    cJSON *request = makeRequest(deleteTaskCurlArgs);

    if (!request) {
      displayMessage("Error making request. Press any key to return to the "
                     "main menu (deleteTask 2).");
      return NULL;
    }

    int toRemoveIdx = 0;
    cJSON *task = NULL;
    cJSON_ArrayForEach(task, tasksJson) {
      char *curId = getJsonValue(task, "id");
      if (strcmp(curId, currentItemId) == 0) {
        break;
      }
      toRemoveIdx++;
    }

    cJSON_DeleteItemFromArray(tasksJson, toRemoveIdx);

    int newLength = cJSON_GetArraySize(tasksJson);
    ITEM **newItems = createItemsFromJson(tasksJson, newLength, "content");
    newItems[newLength] = NULL;

    free(url);

    return newItems;

  } else {
    return NULL;
  }
}
