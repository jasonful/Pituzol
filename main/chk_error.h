#define CHKB(b) do { if (!(b)) { err = ESP_FAIL; goto error;} } while(0)
#define CHKE(e) do { if ((e) != ESP_OK) { goto error;}} while(0);
#define CHK(exp) CHKE(err = (exp))