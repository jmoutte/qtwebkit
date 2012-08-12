"use strict";
/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

var global = {
    argumentsReceived: false,
    scrollbarWidth: null
};

/**
 * @param {!string} id
 */
function $(id) {
    return document.getElementById(id);
}

function bind(func, context) {
    return function() {
        return func.apply(context, arguments);
    };
}

function getScrollbarWidth() {
    if (global.scrollbarWidth === null) {
        var scrollDiv = document.createElement("div");
        scrollDiv.style.opacity = "0";
        scrollDiv.style.overflow = "scroll";
        scrollDiv.style.width = "50px";
        scrollDiv.style.height = "50px";
        document.body.appendChild(scrollDiv);
        global.scrollbarWidth = scrollDiv.offsetWidth - scrollDiv.clientWidth;
        scrollDiv.parentNode.removeChild(scrollDiv);
    }
    return global.scrollbarWidth;
}

/**
 * @param {!string} tagName
 * @param {string=} opt_class
 * @param {string=} opt_text
 * @return {!Element}
 */
function createElement(tagName, opt_class, opt_text) {
    var element = document.createElement(tagName);
    if (opt_class)
        element.setAttribute("class", opt_class);
    if (opt_text)
        element.appendChild(document.createTextNode(opt_text));
    return element;
}

/**
 * @param {!number} width
 * @param {!number} height
 */
function resizeWindow(width, height) {
    if (window.frameElement) {
        window.frameElement.style.width = width + "px";
        window.frameElement.style.height = height + "px";
    } else {
        window.resizeTo(width, height);
    }
}

/**
 * @param {Event} event
 */
function handleMessage(event) {
    initialize(JSON.parse(event.data));
    global.argumentsReceived = true;
}

/**
 * @param {!Object} args
 */
function initialize(args) {
    var main = $("main");
    main.innerHTML = "";
    var errorString = validateArguments(args);
    if (errorString) {
        main.textContent = "Internal error: " + errorString;
        resizeWindow(main.offsetWidth, main.offsetHeight);
    } else
        new ColorPicker(main, args);
}

// The DefaultColorPalette is used when the list of values are empty. 
var DefaultColorPalette = ["#000000", "#404040", "#808080", "#c0c0c0",
    "#ffffff", "#980000", "#ff0000", "#ff9900", "#ffff00", "#00ff00", "#00ffff",
    "#4a86e8", "#0000ff", "#9900ff", "#ff00ff"];

function handleArgumentsTimeout() {
    if (global.argumentsReceived)
        return;
    var args = {
        values : DefaultColorPalette,
        otherColorLabel: "Other..."
    };
    initialize(args);
}

/**
 * @param {!Object} args
 * @return {?string} An error message, or null if the argument has no errors.
 */
function validateArguments(args) {
    if (!args.values)
        return "No values.";
    if (!args.otherColorLabel)
        return "No otherColorLabel.";
    return null;
}

var Actions = {
    ChooseOtherColor: -2,
    Cancel: -1,
    SetValue: 0
};

/**
 * @param {string} value
 */
function submitValue(value) {
    window.pagePopupController.setValueAndClosePopup(Actions.SetValue, value);
}

function handleCancel() {
    window.pagePopupController.setValueAndClosePopup(Actions.Cancel, "");
}

function chooseOtherColor() {
    window.pagePopupController.setValueAndClosePopup(Actions.ChooseOtherColor, "");
}

function ColorPicker(element, config) {
    this._element = element;
    this._config = config;
    if (this._config.values.length === 0)
        this._config.values = DefaultColorPalette;
    this._container = null;
    this._layout();
    document.body.addEventListener("keydown", bind(this._handleKeyDown, this));
    this._element.addEventListener("mousemove", bind(this._handleMouseMove, this));
    this._element.addEventListener("mousedown", bind(this._handleMouseDown, this));
}

var SwatchBorderBoxWidth = 24; // keep in sync with CSS
var SwatchBorderBoxHeight = 24; // keep in sync with CSS
var SwatchesPerRow = 5;
var SwatchesMaxRow = 4;

ColorPicker.prototype._layout = function() {
    var container = createElement("div", "color-swatch-container");
    container.addEventListener("click", bind(this._handleSwatchClick, this), false);
    for (var i = 0; i < this._config.values.length; ++i) {
        var swatch = createElement("button", "color-swatch");
        swatch.dataset.index = i;
        swatch.dataset.value = this._config.values[i];
        swatch.title = this._config.values[i];
        swatch.style.backgroundColor = this._config.values[i];
        container.appendChild(swatch);
    }
    var containerWidth = SwatchBorderBoxWidth * SwatchesPerRow;
    if (this._config.values.length > SwatchesPerRow * SwatchesMaxRow)
        containerWidth += getScrollbarWidth();
    container.style.width = containerWidth + "px";
    container.style.maxHeight = (SwatchBorderBoxHeight * SwatchesMaxRow) + "px";
    this._element.appendChild(container);
    var otherButton = createElement("button", "other-color", this._config.otherColorLabel);
    otherButton.addEventListener("click", chooseOtherColor, false);
    this._element.appendChild(otherButton);
    this._container = container;
    this._otherButton = otherButton;
    var elementWidth = this._element.offsetWidth;
    var elementHeight = this._element.offsetHeight;
    resizeWindow(elementWidth, elementHeight);
};

ColorPicker.prototype.selectColorAtIndex = function(index) {
    index = Math.max(Math.min(this._container.childNodes.length - 1, index), 0);
    this._container.childNodes[index].focus();
};

ColorPicker.prototype._handleMouseMove = function(event) {
    if (event.target.classList.contains("color-swatch"))
        event.target.focus();
};

ColorPicker.prototype._handleMouseDown = function(event) {
    // Prevent blur.
    if (event.target.classList.contains("color-swatch"))
        event.preventDefault();
};

ColorPicker.prototype._handleKeyDown = function(event) {
    var key = event.keyIdentifier;
    if (key === "U+001B") // ESC
        handleCancel();
    else if (key == "Left" || key == "Up" || key == "Right" || key == "Down") {
        var selectedElement = document.activeElement;
        var index = 0;
        if (selectedElement.classList.contains("other-color")) {
            if (key != "Right" && key != "Up")
                return;
            index = this._container.childNodes.length - 1;
        } else if (selectedElement.classList.contains("color-swatch")) {
            index = parseInt(selectedElement.dataset.index, 10);
            switch (key) {
            case "Left":
                index--;
                break;
            case "Right":
                index++;
                break;
            case "Up":
                index -= SwatchesPerRow;
                break;
            case "Down":
                index += SwatchesPerRow;
                break;
            }
            if (index > this._container.childNodes.length - 1) {
                this._otherButton.focus();
                return;
            }
        }
        this.selectColorAtIndex(index);
    }
    event.preventDefault();
};

ColorPicker.prototype._handleSwatchClick = function(event) {
    if (event.target.classList.contains("color-swatch"))
        submitValue(event.target.dataset.value);
};

if (window.dialogArguments) {
    initialize(dialogArguments);
} else {
    window.addEventListener("message", handleMessage, false);
    window.setTimeout(handleArgumentsTimeout, 1000);
}
