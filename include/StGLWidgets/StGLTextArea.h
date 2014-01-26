/**
 * Copyright © 2009-2014 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifndef __StGLTextArea_h_
#define __StGLTextArea_h_

#include <StTemplates/StArrayList.h>

#include <StGL/StGLVertexBuffer.h>
#include <StGL/StGLVec.h>
#include <StGL/StGLTextFormatter.h>
#include <StGLWidgets/StGLShare.h>
#include <StGLWidgets/StGLWidget.h>

/**
 * Class implements basic text rendering widget.
 */
class StGLTextArea : public StGLWidget {

        public:

    typedef enum {
        SIZE_SMALLEST,
        SIZE_SMALL,
        SIZE_NORMAL,
        SIZE_BIG,
        SIZE_BIGGEST,
        SIZE_DOUBLE,
    } FontSize;

        public:

    ST_CPPEXPORT StGLTextArea(StGLWidget* theParent,
                              const int theLeft = 32, const int theTop = 32,
                              const StGLCorner theCorner = StGLCorner(ST_VCORNER_TOP, ST_HCORNER_LEFT),
                              const int theWidth = 256, const int theHeight = 32,
                              const FontSize theSize = StGLTextArea::SIZE_NORMAL);

    ST_CPPEXPORT virtual ~StGLTextArea();

    ST_CPPEXPORT virtual const StString& getClassName();

    /**
     * @return currently drawn text
     */
    ST_CPPEXPORT const StString& getText() const;

    /**
     * @param theText new text to draw
     */
    ST_CPPEXPORT void setText(const StString& theText);

    /**
     * @return extra margin at left side (before text)
     */
    ST_LOCAL inline int getMarginLeft() const {
        return myMarginLeft;
    }

    /**
     * @param theMargin extra margin at left side (before text)
     */
    ST_LOCAL inline void setMarginLeft(const int theMargin) {
        myMarginLeft = theMargin;
    }

    /**
     * @return extra margin at right side (after text)
     */
    ST_LOCAL inline int getMarginRight() const {
        return myMarginRight;
    }

    /**
     * @param theMargin extra margin at right side (after text)
     */
    ST_LOCAL inline void setMarginRight(const int theMargin) {
        myMarginRight = theMargin;
    }

    /**
     * Setup alignment style.
     * @param theAlignX horizontal alignment
     * @param theAlignY vertical   alignment
     */
    inline void setupAlignment(const StGLTextFormatter::StAlignX theAlignX,
                               const StGLTextFormatter::StAlignY theAlignY) {
        myFormatter.setupAlignment(theAlignX, theAlignY);
    }

    /**
     * Setup font style.
     */
    ST_LOCAL void setupStyle(const StFTFont::Style theStyle) {
        myFormatter.setDefaultStyle(theStyle);
    }

    /**
     * @param theToShow - to show border with background or not
     */
    inline void setBorder(const bool theToShow) {
        myToShowBorder = theToShow;
    }

    /**
     * @param theColor- border color
     */
    inline void setBorderColor(const StGLVec3& theColor) {
        myBorderColor.r() = theColor.r();
        myBorderColor.g() = theColor.g();
        myBorderColor.b() = theColor.b();
    }

    /**
     * @param theColor- background color
     */
    inline void setBackColor(const StGLVec3& theColor) {
        myBackColor.r() = theColor.r();
        myBackColor.g() = theColor.g();
        myBackColor.b() = theColor.b();
    }

    /**
     * @param theColor- text color
     */
    inline void setTextColor(const StGLVec3& theColor) {
        myTextColor.r() = theColor.r();
        myTextColor.g() = theColor.g();
        myTextColor.b() = theColor.b();
    }

    /**
     * @param theWidth - text width restriction to force newline (-1 means no restriction)
     */
    ST_CPPEXPORT void setTextWidth(const int theWidth);

    inline const int getFontSize() const {
        switch(mySize) {
            case SIZE_SMALLEST:
                return 10;
            case SIZE_SMALL:
                return 12;
            case SIZE_NORMAL:
                return 16;
            case SIZE_BIG:
                return 18;
            case SIZE_BIGGEST:
                return 20;
            case SIZE_DOUBLE:
                return 28;
            default:
                return mySize;
        }
    }

    ST_CPPEXPORT virtual bool stglInit();
    ST_CPPEXPORT virtual void stglDraw(unsigned int theView);

    /**
     * This method initialize the widget and set it's height to computed formatted text height.
     */
    ST_LOCAL bool stglInitAutoHeight() {
        if(!stglInit()) {
            return false;
        }
        changeRectPx().bottom() = getRectPx().top() + getTextHeight();
        return true;
    }

    /**
     * This method initialize the widget and set it's height to computed formatted text height/width.
     * @param theMaxWidth specifies maximum widget width, -1 means unlimited
     */
    ST_CPPEXPORT bool stglInitAutoHeightWidth(const int theMaxWidth = -1);

    /**
     * Roughly estimate text dimensions.
     */
    ST_CPPEXPORT void computeTextWidth(const GLfloat theWidthMax,
                                       int&          theWidth,
                                       int&          theHeight);

    inline GLint getTextHeight() const {
        return std::abs(GLint(myTextBndBox.height()));
    }

    inline GLint getTextWidth() const {
        return std::abs(GLint(myTextBndBox.width()));
    }

        protected:

    ST_CPPEXPORT void formatText(StGLContext& theCtx);

        private:

    ST_LOCAL void drawText(StGLContext& theCtx);

    ST_LOCAL void recomputeBorder(StGLContext& theCtx);

        private:

    StArrayList<GLuint>                       myTexturesList;
    StArrayList< StHandle<StGLVertexBuffer> > myTextVertBuf;
    StArrayList< StHandle<StGLVertexBuffer> > myTextTCrdBuf;

    StGLVertexBuffer     myBorderIVertBuf;
    StGLVertexBuffer     myBorderOVertBuf;

    class StTextProgram;
    class StBorderProgram;
    StGLShare<StTextProgram>   myTextProgram;
    StGLShare<StBorderProgram> myBorderProgram;

        protected:

    StHandle<StGLFont>   myFont;          //!< used font
    StGLTextFormatter    myFormatter;     //!< text formatter
    StString             myText;          //!< text
    FontSize             mySize;          //!< font size
    StGLVec4             myTextColor;     //!< text   color
    StGLVec4             myShadowColor;   //!< shadow color
    StGLVec4             myBackColor;     //!< text area color
    StGLVec4             myBorderColor;   //!< text area border color

    int                  myMarginLeft;
    int                  myMarginRight;
    int                  myMarginTop;
    int                  myMarginBottom;
    GLfloat              myTextDX;        //!< extra displacement
    StGLRect             myTextBndBox;    //!< text boundary box

    GLfloat              myTextWidth;     //!< text width limit

    bool                 myToRecompute;   //!< flag indicates that text VBOs should be recomputed
    bool                 myToShowBorder;  //!< to show text area border
    bool                 myToDrawShadow;  //!< to render text shadow
    bool                 myIsInitialized;

};

#endif // __StGLTextArea_h_
