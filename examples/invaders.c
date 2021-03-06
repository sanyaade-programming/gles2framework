#include <unistd.h> // usleep

#include "support.h"		// support routines
#include "obj.h"		// loading and displaying wavefront OBJ derived shapes

// window resize function prototype 
void window_size_callback(GLFWwindow* window, int w, int h);

// obj shape textures
GLuint cubeTex, shipTex, alienTex, shotTex, expTex;

// structures holding various pointers and handles for obj shapes
struct obj_t cubeObj, shipObj, alienObj, shotObj;

// pointers to glprint font structures
font_t *font1,*font2;

// matrices and combo matrices
kmMat4 model,rot, view, projection, mvp, vp, mv;

int frame = 0;
float lfps = 0;


kmVec3 pEye, pCenter, pUp;	// "camera" vectors

// direction of light and view for lighting
kmVec3 viewDir,lightDir;

kmVec3 playerPos;
float playerRoll, playerCroll;
int playerFireCount;
float playerPre_error, playerIntegral;

struct playerShot_t {
    kmVec3 pos;
    bool alive;
};

#define MAX_PLAYER_SHOTS 3
struct playerShot_t playerShots[MAX_PLAYER_SHOTS];

struct alien_t {
    kmVec3 pos;
    kmVec3 shot;
    bool alive;
    bool shotActive;
    struct pointCloud_t* explosion;
    bool exploding;
};

#define  MAX_ALIENS 12
struct alien_t aliens[MAX_ALIENS];

struct playerShot_t *getFreeShot()
{
    for (int n = 0; n < MAX_PLAYER_SHOTS; n++) {
        if (playerShots[n].alive == false)
            return &playerShots[n];
    }
    return 0;
}

void resetAliens()
{
    for (int n = 0; n < MAX_ALIENS; n++) {
        aliens[n].pos.x = -5 + n * 2;
        aliens[n].pos.z = -6;
        if (n > 5) {
            aliens[n].pos.x -= 12;
            aliens[n].pos.z = -8;
        }
        aliens[n].alive = true;
        aliens[n].exploding = false;
    }
}

//http://www.embeddedheaven.com/pid-control-algorithm-c-language.htm
//Define parameter
#define epsilon 0.01
#define dt 0.06			//0.01=100ms loop time
#define MAX 4			//For Current Saturation
#define MIN -4
#define Kp 0.1
#define Kd 0.01
#define Ki 0.005

float PIDcal(float setpoint, float actual_position, float *pre_error,
             float *integral)
{
//      static float pre_error = 0;
//      static float integral = 0;
    float error;
    float derivative;
    float output;

    //Caculate P,I,D
    error = setpoint - actual_position;

    //In case of error too small then stop intergration
    if (abs(error) > epsilon) {
        integral[0] = integral[0] + error * dt;
    }
    derivative = (error - pre_error[0]) / dt;
    output = Kp * error + Ki * integral[0] + Kd * derivative;

    //Saturation Filter
    if (output > MAX) {
        output = MAX;
    } else if (output < MIN) {
        output = MIN;
    }
    //Update error
    pre_error[0] = error;

    return output;
}


void resetExposion(struct pointCloud_t* pntC) {

    pntC->tick=0;
    for (int i=0; i<pntC->totalPoints; i++) {
        kmVec3 v;
        kmVec3Fill(&v,rand_range(-1,2),rand_range(-1,2),rand_range(-1,2));
        kmVec3Normalize(&v,&v);

        pntC->vel[i*3]=v.x;
        pntC->vel[i*3+1]=v.y;
        pntC->vel[i*3+2]=v.z;

        pntC->pos[i*3]=0;
        pntC->pos[i*3+1]=0;
        pntC->pos[i*3+2]=0;

    }

}


GLFWwindow* window;
int width=640,height=480; // window width and height

struct timespec ts;  // frame timing

int main()
{

    lightDir.x=0.5;
    lightDir.y=.7;
    lightDir.z=-0.5;
    kmVec3Normalize(&lightDir,&lightDir);

    // creates a window and GLES context
    // create a window and GLES context
	if (!glfwInit())
		exit(EXIT_FAILURE);

	window = glfwCreateWindow(width, height, "I N V A D E R S ! ! !", NULL, NULL);
	if (!window) {
		glfwTerminate();
		exit(EXIT_FAILURE);
	}
	glfwSetWindowSizeCallback(window,window_size_callback);	
	glfwMakeContextCurrent(window);



    // all the shaders have at least texture unit 0 active so
    // activate it now and leave it active
    glActiveTexture(GL_TEXTURE0);

    // The obj shapes and their textures are loaded
    cubeTex = loadPNG("resources/textures/dice.png");
    loadObj(&cubeObj, "resources/models/cube.gbo",
            "resources/shaders/textured.vert", "resources/shaders/textured.frag");


    shipTex = loadPNG("resources/textures/shipv2.png");
    loadObjCopyShader(&shipObj,"resources/models/ship.gbo",&cubeObj);

    alienTex = loadPNG("resources/textures/alien.png");
    loadObjCopyShader(&alienObj, "resources/models/alien.gbo", &cubeObj);

    shotTex = loadPNG("resources/textures/shot.png");
    loadObjCopyShader(&shotObj, "resources/models/shot.gbo", &cubeObj);

    expTex = loadPNG("resources/textures/explosion.png");


    playerPos.x = 0;
    playerPos.y = 0;
    playerPos.z = 0;

    kmMat4Identity(&view);

    pEye.x = 0;
    pEye.y = 2;
    pEye.z = 4;
    pCenter.x = 0;
    pCenter.y = 0;
    pCenter.z = -5;
    pUp.x = 0;
    pUp.y = 1;
    pUp.z = 0;

    kmMat4LookAt(&view, &pEye, &pCenter, &pUp);

    // projection matrix, as distance increases
    // the way the model is drawn is effected
    kmMat4Identity(&projection);
    kmMat4PerspectiveProjection(&projection, 45,
                                (float)width/ height, 0.1, 1000);

    glViewport(0, 0, width,height);

    // these two matrices are pre combined for use with each model render
    kmMat4Assign(&vp, &projection);
    kmMat4Multiply(&vp, &vp, &view);

    // initialises glprint's matrix shader and texture
    initGlPrint(width,height);

	font1=createFont("resources/textures/font.png",0,256,16,16,16);
	font2=createFont("resources/textures/bigfont.png",32,512,9.5,32,48);

    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);	// only used by glprintf
    glEnable(GL_DEPTH_TEST);


    int num_frames = 0;

    bool quit = false;

    resetAliens();

    for (int n = 0; n < MAX_PLAYER_SHOTS; n++) {
        playerShots[n].alive = false;
    }


    initPointClouds("resources/shaders/particle.vert",
                    "resources/shaders/particle.frag",(float)width/24.0);



    for (int n = 0; n < MAX_ALIENS; n++) {
        aliens[n].explosion=createPointCloud(40);
        resetExposion(aliens[n].explosion); // sets initials positions
    }

	glClearColor(0, .5, 1, 1);

    while (!quit) {		// the main loop

        clock_gettime(0,&ts);  // note the time BEFORE we start to render the current frame
		glfwPollEvents();


        if (glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS || glfwWindowShouldClose(window))
            quit = true;

        float rad;		// radians rotation based on frame counter

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        frame++;
        rad = frame * (0.0175f * 2);

        //kmMat4Identity(&model);
        kmMat4Translation(&model, playerPos.x, playerPos.y, playerPos.z);
        playerCroll +=
            (PIDcal(playerRoll, playerCroll, &playerPre_error, &playerIntegral)
             / 2);
//        kmMat4RotationPitchYawRoll(&model, 0, 3.1416, playerCroll * 3);	//
		kmMat4RotationYawPitchRoll(&rot,0,3.1416,-playerCroll*3);
        kmMat4Multiply(&model, &model, &rot);
        
        kmMat4Assign(&mvp, &vp);
        kmMat4Multiply(&mvp, &mvp, &model);

        kmMat4Assign(&mv, &view);
        kmMat4Multiply(&mv, &mv, &model);

        glBindTexture(GL_TEXTURE_2D, shipTex);
        drawObj(&shipObj, &mvp, &mv, lightDir, viewDir);

        glPrintf(50 + sinf(rad) * 16, 240 + cosf(rad) * 16,
                 font2,"frame=%i", frame);

        kmVec3 tmp;

        playerFireCount--;

        if (glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS && playerFireCount < 0) {

            struct playerShot_t *freeShot;
            freeShot = getFreeShot();
            if (freeShot != 0) {
                playerFireCount = 15;
                freeShot->alive = true;
                kmVec3Assign(&freeShot->pos, &playerPos);
            }
        }

        for (int n = 0; n < MAX_PLAYER_SHOTS; n++) {
            if (playerShots[n].alive) {
                playerShots[n].pos.z -= .08;
                if (playerShots[n].pos.z < -10)
                    playerShots[n].alive = false;

                //kmMat4Identity(&model);
                kmMat4Translation(&model, playerShots[n].pos.x,
                                  playerShots[n].pos.y,
                                  playerShots[n].pos.z);
                //kmMat4RotationPitchYawRoll(&model, rad * 4, 0,
                //                           -rad * 4);
				kmMat4RotationYawPitchRoll(&rot,rad*4,0,-rad*4);
				kmMat4Multiply(&model,&model,&rot);
                kmMat4Assign(&mvp, &vp);
                kmMat4Multiply(&mvp, &mvp, &model);

                kmMat4Assign(&mv, &view);
                kmMat4Multiply(&mv, &mv, &model);

                glBindTexture(GL_TEXTURE_2D, shotTex);
                drawObj(&shotObj, &mvp, &mv, lightDir, viewDir);
            }
        }

        playerRoll = 0;
        if (glfwGetKey(window,GLFW_KEY_LEFT)==GLFW_PRESS && playerPos.x > -10) {
            playerPos.x -= 0.1;
            playerRoll = .2;
        }
        if (glfwGetKey(window,GLFW_KEY_RIGHT)==GLFW_PRESS && playerPos.x < 10) {
            playerPos.x += 0.1;
            playerRoll = -.2;
        }
        pEye.x = playerPos.x * 1.25;

        pCenter.x = playerPos.x;
        pCenter.y = playerPos.y + 1;
        pCenter.z = playerPos.z;

        int deadAliens;

        deadAliens = 0;

        for (int n = 0; n < MAX_ALIENS; n++) {
            if (aliens[n].alive == true) {

                //kmMat4Identity(&model);
                kmMat4Translation(&model, aliens[n].pos.x,
                                  aliens[n].pos.y, aliens[n].pos.z);
                //kmMat4RotationPitchYawRoll(&model, -.4, 0, 0);
				kmMat4RotationYawPitchRoll(&rot,.2,0,0);
				kmMat4Multiply(&model,&model,&rot);

                kmMat4Assign(&mvp, &vp);
                kmMat4Multiply(&mvp, &mvp, &model);

                kmMat4Assign(&mv, &view);
                kmMat4Multiply(&mv, &mv, &model);

                glBindTexture(GL_TEXTURE_2D, alienTex);
                drawObj(&alienObj, &mvp, &mv, lightDir, viewDir);

                kmVec3 d;
                for (int i = 0; i < MAX_PLAYER_SHOTS; i++) {
                    kmVec3Subtract(&d, &aliens[n].pos,
                                   &playerShots[i].pos);
                    if (kmVec3Length(&d) < .7
                            && playerShots[i].alive) {
                        aliens[n].alive = false;
                        playerShots[i].alive = false;
                        aliens[n].exploding = true;
                        resetExposion(aliens[n].explosion);
                    }
                }
            } 

            if (aliens[n].alive != true && aliens[n].exploding != true) {
                    deadAliens++;
                
            }
        }

        if (deadAliens == MAX_ALIENS) {
            resetAliens();
        }

		// draw explosions after ALL aliens
        for (int n = 0; n < MAX_ALIENS; n++) {
			if (aliens[n].exploding==true) {
				kmMat4Identity(&model);
				kmMat4Translation(&model, aliens[n].pos.x,
								  aliens[n].pos.y, aliens[n].pos.z);

				kmMat4Assign(&mvp, &vp);
				kmMat4Multiply(&mvp, &mvp, &model);
				glBindTexture(GL_TEXTURE_2D, expTex);
				drawPointCloud(aliens[n].explosion, &mvp);
				aliens[n].explosion->tick=aliens[n].explosion->tick+0.05;
				if (aliens[n].explosion->tick>1.25) {
					aliens[n].exploding=false;                   	
				} else {
					// update the explosion
					
					for (int i=0; i<aliens[n].explosion->totalPoints; i++) {
				
						float t;
						t=aliens[n].explosion->tick;
						if (i>aliens[n].explosion->totalPoints/2) t=t/2.0;
						aliens[n].explosion->pos[i*3]=aliens[n].explosion->vel[i*3] * t;
						aliens[n].explosion->pos[i*3+1]=aliens[n].explosion->vel[i*3+1] * t;
						aliens[n].explosion->pos[i*3+2]=aliens[n].explosion->vel[i*3+2] * t;
				
					}
				}
			}
		}

        // move camera
        kmMat4LookAt(&view, &pEye, &pCenter, &pUp);

        kmMat4Assign(&vp, &projection);
        kmMat4Multiply(&vp, &vp, &view);

        kmVec3Subtract(&viewDir,&pEye,&pCenter);
        kmVec3Normalize(&viewDir,&viewDir);

        // dump values
        glPrintf(100, 280, font1,"eye    %3.2f %3.2f %3.2f ", pEye.x, pEye.y, pEye.z);
        glPrintf(100, 296, font1,"centre %3.2f %3.2f %3.2f ", pCenter.x, pCenter.y,
                 pCenter.z);
        glPrintf(100, 340, font1,"frame %i %i ", frame, frame % 20);



        glfwSwapBuffers(window);

        ts.tv_nsec+=20000000;  // 1000000000 / 50 = 50hz less time to render the frame
        //thrd_sleep(&ts,NULL); // tinycthread
        usleep(20000); // while I work out why tinycthread that was working isnt.... :/

    }

	glfwDestroyWindow(window);
	glfwTerminate();

    return 0;
}

void window_size_callback(GLFWwindow* window, int w, int h)
{
	width=w;height=h;

    glViewport(0, 0, width, height);

    // projection matrix, as distance increases
    // the way the model is drawn is effected
    kmMat4Identity(&projection);
    kmMat4PerspectiveProjection(&projection, 45,
                                (float)width / height, 0.1, 1000);

	reProjectGlPrint(width,height); // updates the projection matrix used by glPrint
	reProjectSprites(width,height); // updates the projection matrix used by the sprites
	resizePointCloudSprites((float)width/24.0);

}


