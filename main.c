#include <curl/curl.h>
#include <curl/easy.h>

int main(void) {
  CURL *curl = curl_easy_init();
  if (curl) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "https://google.com");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
}
