#include "Instance.hpp"

/////////////////////////////////////////////
// mlomb-bot
/////////////////////////////////////////////

int main() {
	Instance* instance = new Instance();

	instance->Initialize("mlomb-bot-v18");
	instance->Play();

	delete instance;

	return 0;
}