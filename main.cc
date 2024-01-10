#define SHOW_CONSOLE

#include <graphics.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <fstream>

mouse_msg gMouseMsg = { 0 };
key_msg gKeyMsg = { 0 };
std::map <std::string, PIMAGE> res;

const int scale = 32;
std::string map[32][32] = {};
std::string turn = "black";
int current_x = -1, current_y = -1;
// 加载函数，用于初始化资源
void load (void);
// 事件更新函数，主要用来接收事件，比如鼠标键盘事件
void eventUpdate (void);
// 输出更新函数，主要用于处理输入
void dataUpdate (void);
// 画面绘制函数，主要用于显示输出
void drawInterface (void);

int main (int argc, char *argv []) {
	// 手动刷新模式
	setinitmode (INIT_RENDERMANUAL);
	// 界面分辨率
	initgraph (800, 600);
	// setbkmode (TRANSPARENT);
	load ();
	for (;is_run (); delay_fps (60), cleardevice ()) {

		eventUpdate ();
		dataUpdate ();
		drawInterface ();
	}

	closegraph ();
	return 0;
}

void load (void) {
	// 从外部读入文件名和路径数据，然后往map中写入数据
	PIMAGE tmp = newimage ();
	getimage_pngfile (tmp, "./res/chesses.png");
	std::fstream file;
	file.open ("chesses.txt", std::ios::in);
	std::string str;
	int x, y, w, h;
	while (file >> str >> x >> y >> w >> h) {
		// std::cout << str << " " << x << " " << y << " " << w << " " << h << std::endl;
		if (res.count(str)==0) {
			res.insert ({str, newimage()});
			getimage (res [str], tmp, x, y, w, h);
		} else {
			throw "you have inserted same name image!";
		}
	}
	file.close ();

	for (int i = 0; i < 32; i++) {
		for (int j = 0; j <32; j++) {
			map[i][j] = "empty";
		}
	}
	// delimage (tmp);
	srand(time(nullptr));
}
void eventUpdate (void) {
//
	while (mousemsg ()) {
		gMouseMsg = getmouse ();
		double distance = std::hypot(gMouseMsg.x-current_x*scale-scale/2,
									gMouseMsg.y-current_y*scale-scale/2);
		if (distance > 22) {
			std::cout << distance << std::endl;
			current_x = gMouseMsg.x/scale;
			current_y = gMouseMsg.y/scale;
		}
		if (gMouseMsg.is_down() && gMouseMsg.is_left()) {
			if (map[current_y][current_x] == "empty") {
				map[current_y][current_x] = turn;
				if (turn == "black") {
					turn = "white";
				} else {
					turn = "black";
				}
			} else {

			}
		}
	}
	while (kbmsg ()) {
		gKeyMsg = getkey ();
	}
}
void dataUpdate (void) {
//
}
void drawInterface (void) {
//
	setfillcolor (EGERGB(247,165,47));
	bar (0,0,getwidth(),getheight());

	for (int i = 0; i < 32; i++) {
		ege::setcolor(EGERGB(0,0,0));
		ege::line(0, i*scale, 32*scale, i*scale);
		ege::line(i*scale, 0, i*scale, 32*scale);

		for (int j = 0; j <32; j++) {
			ege::putimage_withalpha(nullptr, res[map[i][j]], j*scale,i*scale);
		}
	}
	ege::setcolor(EGERGB(255,255,255));
	xyprintf(0,0,"%f", ege::getfps());
	if (map[current_y][current_x] == "empty") {
		ege::putimage_withalpha(nullptr, res[turn], current_x*scale, current_y*scale);
	}

}
