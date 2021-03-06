#include "support.h"

#include "lodepng.h"


float rand_range(float start,float range) {
    return start + range * ((float)rand() / RAND_MAX) ;
}

int loadPNG(const char *filename)
{
    unsigned error;
    unsigned char* image;
    unsigned w, h;
    GLuint texture;
    LodePNGState state;
    unsigned char* png;
    size_t pngsize;
    size_t components;

    lodepng_state_init(&state);
    lodepng_load_file(&png, &pngsize, filename);
    error = lodepng_inspect(&w, &h, &state, png, 1024);
    if (error) return 0;

    GLenum glcolortype = GL_RGBA;
    LodePNGColorMode colormode = state.info_png.color;
    unsigned bitdepth = colormode.bitdepth;
    LodePNGColorType colortype = colormode.colortype;
    switch(colortype)
    {
    case LCT_GREY:
        glcolortype = GL_LUMINANCE;
        error = lodepng_decode_memory(&image, &w, &h, png, pngsize, colortype, bitdepth);
        components = 1;
        break;
    case LCT_GREY_ALPHA:
        glcolortype = GL_LUMINANCE_ALPHA;
        error = lodepng_decode_memory(&image, &w, &h, png, pngsize, colortype, bitdepth);
        components = 2;
        break;
    case LCT_RGB:
        glcolortype = GL_RGB;
        error = lodepng_decode24(&image, &w, &h, png, pngsize);
        components = 3;
        break;
    case LCT_RGBA:
        glcolortype = GL_RGBA;
        error = lodepng_decode32(&image, &w, &h, png, pngsize);
        components = 4;
        break;
    case LCT_PALETTE:
    default:
        glcolortype = GL_RGBA;
        error = lodepng_decode32(&image, &w, &h, png, pngsize);
        components = 4;
        break;
    }

    if(error)
    {
        printf("PNG decoder error %u: %s\n", error, lodepng_error_text(error));
        return 0;
    }

    free(png);

    // Texture size must be power of 2 (for some implementations of GLES)
    // Find next power of two
    size_t u2 = 1;
    while(u2 < w) u2 *= 2;
    size_t v2 = 1;
    while(v2 < h) v2 *= 2;

    // Make power of two version of the image.
    unsigned char *image2 = (unsigned char *)malloc(sizeof(unsigned char) * u2 * v2 * components);
    for(size_t y = 0; y < h; y++)
    {
        for(size_t x = 0; x < w; x++)
        {
            for(size_t c = 0; c < components; c++)
            {
                unsigned char val = image[components * w * y + components * x + c];
                image2[components * u2 * y + components * x + c] = val;
            }
        }
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, glcolortype, u2, v2, 0, glcolortype, GL_UNSIGNED_BYTE, image2);

    /*cleanup*/
    lodepng_state_cleanup(&state);
    free(image2);
    free(image);

    return texture;
}


GLuint getShaderLocation(int type, GLuint prog, const char *name)
{
    GLuint ret;
    if (type == shaderAttrib)
        ret = glGetAttribLocation(prog, name);
    if (type == shaderUniform)
        ret = glGetUniformLocation(prog, name);
    if (ret == -1) {
        printf("Cound not bind shader location %s\n", name);
    }
    return ret;
}

/**
 * Store all the file's contents in memory, useful to pass shaders
 * source code to OpenGL
 */
char *file_read(const char *filename)
{
    FILE *in = fopen(filename, "rb");
    if (in == NULL)
        return NULL;

    int res_size = BUFSIZ;
    char *res = (char *)malloc(res_size);
    int nb_read_total = 0;

    while (!feof(in) && !ferror(in)) {
        if (nb_read_total + BUFSIZ > res_size) {
            if (res_size > 10 * 1024 * 1024)
                break;
            res_size = res_size * 2;
            res = (char *)realloc(res, res_size);
        }
        char *p_res = res + nb_read_total;
        nb_read_total += fread(p_res, 1, BUFSIZ, in);
    }

    fclose(in);
    res = (char *)realloc(res, nb_read_total + 1);
    res[nb_read_total] = '\0';
    return res;
}

/**
 * Display compilation errors from the OpenGL shader compiler
 */
void print_log(GLuint object)
{
    GLint log_length = 0;
    if (glIsShader(object))
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
    else if (glIsProgram(object))
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
    else {
        fprintf(stderr, "printlog: Not a shader or a program\n");
        return;
    }

    char *log = (char *)malloc(log_length);

    if (glIsShader(object))
        glGetShaderInfoLog(object, log_length, NULL, log);
    else if (glIsProgram(object))
        glGetProgramInfoLog(object, log_length, NULL, log);

    fprintf(stderr, "%s", log);
    free(log);
}

/**
 * Compile the shader from file 'filename', with error handling
 */
GLuint create_shader(const char *filename, GLenum type)
{
    const GLchar *source = file_read(filename);
    if (source == NULL) {
        fprintf(stderr, "Error opening %s: ", filename);
        perror("");
        return 0;
    }
    GLuint res = glCreateShader(type);
    const GLchar *sources[] = {
        // Define GLSL version
#ifdef GL_ES_VERSION_2_0
        "#version 100\n"
#else
        "#version 120\n"
#endif
        ,
        // GLES2 precision specifiers
#ifdef GL_ES_VERSION_2_0
        // Define default float precision for fragment shaders:
        (type == GL_FRAGMENT_SHADER) ?
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
        "precision highp float;           \n"
        "#else                            \n"
        "precision mediump float;         \n"
        "#endif                           \n" : ""
        // Note: OpenGL ES automatically defines this:
        // #define GL_ES
#else
        // Ignore GLES 2 precision specifiers:
        "#define lowp   \n" "#define mediump\n" "#define highp  \n"
#endif
        ,
        source
    };
    glShaderSource(res, 3, sources, NULL);
    free((void *)source);

    glCompileShader(res);
    GLint compile_ok = GL_FALSE;
    glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
    if (compile_ok == GL_FALSE) {
        fprintf(stderr, "%s:", filename);
        print_log(res);
        glDeleteShader(res);
        return 0;
    }

    return res;
}


struct {  // blob of globals for the glPrint routine
    kmMat4 opm, otm, t;
    GLuint printProg, opm_uniform;
    GLuint fonttex, texture_uniform, cx_uniform, cy_uniform;
    GLuint vert_attrib, uv_attrib;
    GLuint quadvbo, texvbo;
} __glp;

void initGlPrint(int w, int h)
{

	reProjectGlPrint(w,h);
    
    GLuint vs, fs;
    vs = create_shader("resources/shaders/glprint.vert", GL_VERTEX_SHADER);
    fs = create_shader("resources/shaders/glprint.frag", GL_FRAGMENT_SHADER);

    __glp.printProg = glCreateProgram();
    glAttachShader(__glp.printProg, vs);
    glAttachShader(__glp.printProg, fs);
    glLinkProgram(__glp.printProg);
    int link_ok;
    glGetProgramiv(__glp.printProg, GL_LINK_STATUS, &link_ok);
    if (!link_ok) {
        printf("glLinkProgram:");
        print_log(__glp.printProg);
        printf("\n");
    }

    __glp.cx_uniform = getShaderLocation(shaderUniform, __glp.printProg, "cx");
    __glp.cy_uniform = getShaderLocation(shaderUniform, __glp.printProg, "cy");
    __glp.opm_uniform =
        getShaderLocation(shaderUniform, __glp.printProg, "opm_uniform");
    __glp.texture_uniform =
        getShaderLocation(shaderUniform, __glp.printProg, "texture_uniform");

    __glp.vert_attrib = getShaderLocation(shaderAttrib, __glp.printProg, "vert_attrib");
    __glp.uv_attrib = getShaderLocation(shaderAttrib, __glp.printProg, "uv_attrib");

}

void reProjectGlPrint(int w,int h) {
	kmMat4OrthographicProjection(&__glp.opm, 0, w, h, 0, -10, 10);
} 


font_t* createFont(const char* tpath,int cbase,float tHeight,float tLines,int fWidth,int fHeight) {
    font_t *t=malloc(sizeof(font_t));

    t->tex = loadPNG(tpath);
    t->base=cbase;
    t->tHeight=tHeight;
    t->tLines=tLines;
    t->fWidth=fWidth;
    t->fHeight=fHeight;

    float *vb=malloc(sizeof(float) * 3 * 6);

    vb[0]=vb[1]=vb[2]=vb[5]=vb[7]=vb[8]=vb[11]=vb[12]=vb[13]=vb[14]=vb[15]=vb[17]=0;
    vb[3]=vb[6]=vb[9]=fWidth;
    vb[4]=vb[10]=vb[16]=fHeight;

    glGenBuffers(1, &t->vertBuf);
    glBindBuffer(GL_ARRAY_BUFFER, t->vertBuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * 6, vb, GL_STATIC_DRAW);

    free(vb);

    float *tc=malloc(sizeof(float) * 2 * 6);
    tc[0]=tc[1]=tc[5]=tc[8]=tc[9]=tc[10]=0;
    tc[2]=tc[4]=tc[6]=1./16;
    tc[3]=tc[7]=tc[11]=1./tLines;

    glGenBuffers(1, &t->texBuf);
    glBindBuffer(GL_ARRAY_BUFFER, t->texBuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 6, tc, GL_STATIC_DRAW);

    free(tc);

    return t;
}

void glPrintf(float x, float y, font_t *fnt, const char *fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(text, fmt, ap);
    va_end(ap);

    glUseProgram(__glp.printProg);
    kmMat4Assign(&__glp.otm, &__glp.opm);
    kmMat4Translation(&__glp.t, x, y, -1);
    kmMat4Multiply(&__glp.otm, &__glp.otm, &__glp.t);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glBindTexture(GL_TEXTURE_2D, fnt->tex);

    glEnableVertexAttribArray(__glp.vert_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, fnt->vertBuf);
    glVertexAttribPointer(__glp.vert_attrib, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glEnableVertexAttribArray(__glp.uv_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, fnt->texBuf);
    glVertexAttribPointer(__glp.uv_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glUniform1i(__glp.texture_uniform, 0);

    for (int n = 0; n < strlen(text); n++) {

        int c = (int)text[n]-fnt->base;
        float cx = c % 16;
        float cy = (int)(c/16.0);
        cy = cy * (1. / (fnt->tLines));
        cx = cx * (1. / 16);

        glUniformMatrix4fv(__glp.opm_uniform, 1, GL_FALSE, (GLfloat *) & __glp.otm);
        glUniform1f(__glp.cx_uniform, cx);
        glUniform1f(__glp.cy_uniform, cy);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        kmMat4Translation(&__glp.t, fnt->fWidth, 0, 0);
        kmMat4Multiply(&__glp.otm, &__glp.otm, &__glp.t);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisableVertexAttribArray(__glp.uv_attrib);
    glDisableVertexAttribArray(__glp.vert_attrib);

}


struct {  // blob of globals for the sprite routine
    kmMat4 opm, otm, t, r;
    GLuint spriteProg, opm_uniform;
    GLuint texture_uniform, cx_uniform, cy_uniform, u_size;
    GLuint vert_attrib, uv_attrib;
    GLuint quadvbo, texvbo;
} __spr;



void initSprite(int w, int h)
{
    const GLfloat quadVertices[] = {
        -.5,	-.5,	0,
        .5,  	 .5,	0,
        .5,		-.5,	0,

        .5,  	 .5,	0,
        -.5,	-.5,	0,
        -.5,	 .5,	0
    };

    const GLfloat texCoord[] = {
        0,		0,
        1.,		1,
        1.,		0,
        1.,		1.,
        0,		0,
        0,		1.
    };


	reProjectSprites(w,h);

    GLuint vs, fs;
    vs = create_shader("resources/shaders/sprite.vert", GL_VERTEX_SHADER);
    fs = create_shader("resources/shaders/sprite.frag", GL_FRAGMENT_SHADER);

    __spr.spriteProg = glCreateProgram();
    glAttachShader(__spr.spriteProg, vs);
    glAttachShader(__spr.spriteProg, fs);
    glLinkProgram(__spr.spriteProg);
    int link_ok;
    glGetProgramiv(__spr.spriteProg, GL_LINK_STATUS, &link_ok);
    if (!link_ok) {
        printf("glLinkProgram:");
        print_log(__spr.spriteProg);
        printf("\n");
    }

    __spr.u_size = getShaderLocation(shaderUniform, __spr.spriteProg, "u_size");
    __spr.opm_uniform =
        getShaderLocation(shaderUniform, __spr.spriteProg, "opm_uniform");
    __spr.texture_uniform =
        getShaderLocation(shaderUniform, __spr.spriteProg, "texture_uniform");

    __spr.vert_attrib = getShaderLocation(shaderAttrib, __spr.spriteProg, "vert_attrib");
    __spr.uv_attrib = getShaderLocation(shaderAttrib, __spr.spriteProg, "uv_attrib");


    glGenBuffers(1, &__spr.quadvbo);
    glBindBuffer(GL_ARRAY_BUFFER, __spr.quadvbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * 6, quadVertices,
                 GL_STATIC_DRAW);

    glGenBuffers(1, &__spr.texvbo);
    glBindBuffer(GL_ARRAY_BUFFER, __spr.texvbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 6, texCoord,
                 GL_STATIC_DRAW);

}

void reProjectSprites(int w, int h) {
    kmMat4OrthographicProjection(&__spr.opm, 0, w, h, 0, -10, 10); // support layers ?	
}

void drawSprite(float x, float y, float w, float h, float a, int tex)
{
    glUseProgram(__spr.spriteProg);
    kmMat4Assign(&__spr.otm, &__spr.opm);
    kmMat4Translation(&__spr.t, x, y, -1); // support z layers?
    kmMat4RotationZ(&__spr.r,a);
    kmMat4Multiply(&__spr.t,&__spr.t,&__spr.r);
    kmMat4Multiply(&__spr.otm, &__spr.otm, &__spr.t);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    glBindTexture(GL_TEXTURE_2D, tex);

    glEnableVertexAttribArray(__spr.vert_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, __spr.quadvbo);
    glVertexAttribPointer(__spr.vert_attrib, 3, GL_FLOAT, GL_FALSE, 0, 0);

    glEnableVertexAttribArray(__spr.uv_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, __spr.texvbo);
    glVertexAttribPointer(__spr.uv_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glUniform1i(__spr.texture_uniform, 0);
    glUniform2f(__spr.u_size, w,h);


    glUniformMatrix4fv(__spr.opm_uniform, 1, GL_FALSE, (GLfloat *) & __spr.otm);

    glDrawArrays(GL_TRIANGLES, 0, 6);


    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisableVertexAttribArray(__glp.uv_attrib);
    glDisableVertexAttribArray(__glp.vert_attrib);

}





struct __pointGlobs {
    int Partprogram,part_mvp_uniform,part_tex_attrib;
    int part_tex_uniform,part_vert_attrib;
    int part_size_uniform;
} __pg;

// My intel i5 (intel HD4000) seems to be missing this - who to report to
// messa?, kernel driver team?
// this value however does work!!!
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

void drawPointCloud(struct pointCloud_t* pntC, kmMat4* mat) {

    glUseProgram(__pg.Partprogram);
    glUniformMatrix4fv(__pg.part_mvp_uniform, 1, GL_FALSE, (GLfloat *) mat);
    glUniform1i(__pg.part_tex_uniform, 0);

// least sucky depth fudge!
    glEnable(GL_POINTS);
    glEnable(GL_PROGRAM_POINT_SIZE);
//    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glEnableVertexAttribArray(__pg.part_vert_attrib);
    glBindBuffer(GL_ARRAY_BUFFER, pntC->vertBuf);
    glBufferSubData(GL_ARRAY_BUFFER, 0,sizeof(float)*3*pntC->totalPoints, pntC->pos);
    glVertexAttribPointer(__pg.part_vert_attrib,3,GL_FLOAT,GL_FALSE,0,0);
    glDrawArrays(GL_POINTS,0,pntC->totalPoints);
    glDisableVertexAttribArray(pntC->vertBuf);
    glDisable(GL_POINTS);
    //glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);



}


struct pointCloud_t* createPointCloud(int size) {

    struct pointCloud_t* pntC=malloc(sizeof(struct pointCloud_t));
    pntC->totalPoints=size;
    pntC->pos=malloc(size*sizeof(float)*3);
    pntC->vel=malloc(size*sizeof(float)*3);


    glGenBuffers(1, &pntC->vertBuf);
    glBindBuffer(GL_ARRAY_BUFFER, pntC->vertBuf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3 * pntC->totalPoints, pntC->pos,
                 GL_DYNAMIC_DRAW);

    return pntC;
}

void freePointCloud(struct pointCloud_t* pntC) {
    free(pntC->pos);
    free(pntC->vel);
    free(pntC);
}

void initPointClouds(const char* vertS, const char* fragS, float pntSize) {

    GLint link_ok = GL_FALSE;

    GLuint vs, fs;
    vs = create_shader(vertS, GL_VERTEX_SHADER);
    fs = create_shader(fragS, GL_FRAGMENT_SHADER);
    __pg.Partprogram = glCreateProgram();

    glAttachShader(__pg.Partprogram, vs);
    glAttachShader(__pg.Partprogram, fs);
    glLinkProgram(__pg.Partprogram);
    glGetProgramiv(__pg.Partprogram, GL_LINK_STATUS, &link_ok);
    if (!link_ok) {
        printf("particle glLinkProgram error \n");
        print_log(__pg.Partprogram);
        //    return 0;
    }

    __pg.part_vert_attrib =
        getShaderLocation(shaderAttrib, __pg.Partprogram, "vertex_attrib");
    __pg.part_mvp_uniform =
        getShaderLocation(shaderUniform, __pg.Partprogram, "mvp_uniform");
    __pg.part_tex_uniform =
        getShaderLocation(shaderUniform, __pg.Partprogram, "u_texture");

	__pg.part_size_uniform =
	getShaderLocation(shaderUniform, __pg.Partprogram, "u_point_size");
	
	resizePointCloudSprites(pntSize);
}

void resizePointCloudSprites(float s) {
    glUseProgram(__pg.Partprogram);
    glUniform1f(__pg.part_size_uniform, s);
}
