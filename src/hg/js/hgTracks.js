// Javascript for use in hgTracks CGI

// "use strict";

var debug = false;
var browser;              // browser ("msie", "safari" etc.) // move to utils.js?

/* Data passed in from CGI via the hgTracks object:
 *
 * string cgiVersion      // CGI_VERSION
 * string chromName       // current chromosome
 * int winStart           // genomic start coordinate (0-based, half-open)
 * int winEnd             // genomic end coordinate
 * int newWinWidth        // new width (in bps) if user clicks on the top ruler
 * boolean revCmplDisp    // true if we are in reverse display
 * int insideX            // width of side-bar (in pixels)
 * int rulerClickHeight   // height of ruler (in pixels) - zero if ruler is hidden
 * boolean inPlaceUpdate  // true if in-place-update is turned on
 * int imgBox*            // various drag-scroll values
 * boolean measureTiming  // true if measureTiming is on
 * Object trackDb         // hash of trackDb entries for tracks which are visible on current page
 */

function initVars()
{  // There are various entry points, so we call initVars in several places to make sure all is well
    if (typeof(hgTracks) != "undefined" && !genomePos.original) {
        // remember initial position and size so we can restore it if user cancels
        genomePos.original = genomePos.getOriginalPos();
        genomePos.originalSize = $('#size').text();
        dragSelect.originalCursor = jQuery('body').css('cursor');

        imageV2.imgTbl = $('#imgTbl');
        // imageV2.enabled == true unless: advancedJavascript==false, or trackSearch, or config pg
        imageV2.enabled = (imageV2.imgTbl != undefined && imageV2.imgTbl.length > 0);

        jQuery.each(jQuery.browser, function(i, val) {
            if(val) {
                browser = i;
            }
            });
        // jQuery load function with stuff to support drag selection in track img
        if(browser == "safari") {
            if(navigator.userAgent.indexOf("Chrome") != -1) {
                // Handle the fact that (as of 1.3.1), jQuery.browser reports "safari" when the browser is in fact Chrome.
                browser = "chrome";
            } else {
                // Safari has the following bug: if we update the hgTracks map dynamically, the browser ignores the changes (even
                // though if you look in the DOM the changes are there). So we have to do a full form submission when the
                // user changes visibility settings or track configuration.
                // As of 5.0.4 (7533.20.27) this is problem still exists in safari.
                // As of 5.1 (7534.50) this problem appears to have been fixed - unfortunately, logs for 7/2011 show vast majority of safari users
                // are pre-5.1 (5.0.5 is by far the most common).
                //
                // Early versions of Chrome had this problem too, but this problem went away as of Chrome 5.0.335.1 (or possibly earlier).
                imageV2.mapIsUpdateable = false;
                var reg = new RegExp("Version\/(\[0-9]+\.\[0-9]+) Safari");
                var a = reg.exec(navigator.userAgent);
                if(a && a[1]) {
                    var version = Number(a[1]);
                    if(version >= 5.1) {
                        imageV2.mapIsUpdateable = true;
                    }
                }
            }
        }
        imageV2.inPlaceUpdate = hgTracks.inPlaceUpdate && imageV2.mapIsUpdateable;
    }
}

  /////////////////////////////////////
 ////////// Genomic position /////////
/////////////////////////////////////
var genomePos = {

    original: null,
    originalSize: 0,

    linkFixup: function (pos, id, reg, endParamName)
    {   // fixup external links (e.g. ensembl)
        var ele = $(document.getElementById(id));
        if(ele.length) {
            var link = ele.attr('href');
            var a = reg.exec(link);
            if(a && a[1]) {
                ele.attr('href', a[1] + pos.start + "&" + endParamName + "=" + pos.end);
            }
        }
    },

    setByCoordinates: function (chrom, start, end)
    {
        var newPosition = chrom + ":" + commify(start) + "-" + commify(end);
        genomePos.set(newPosition, commify(end - start + 1));
        return newPosition;
    },

    getElement: function ()
    {
    // Return position box object
        var tags = document.getElementsByName("position");
        // There are multiple tags with name == "position" (the visible position text input
        // and a hidden with id='positionHidden'); we return value of visible element.
        for (var i = 0; i < tags.length; i++) {
                var ele = tags[i];
                if(ele.id != "positionHidden") {
                    return ele;
                }
        }
        return null;
    },

    get: function ()
    {
    // Return current value of position box
        var ele = genomePos.getElement();
        if(ele != null) {
            return ele.value;
        }
        return null;
    },

    getOriginalPos: function ()
    {
        return genomePos.original || genomePos.get();
    },

    revertToOriginalPos: function ()
    {
    // undo changes to position (i.e. after user aborts a drag-and-select).
        this.set(this.original, this.originalSize);
        this.original = this.originalSize = null;         // not sure if this is necessary.
    },

    set: function (position, size)
    {   // Set value of position and size (in hiddens and input elements).
        // We assume size has already been commified.
        // Either position or size may be null.
        if(position) {
            // There are multiple tags with name == "position"
            // (one in TrackHeaderForm and another in TrackForm).
            var tags = document.getElementsByName("position");
            for (var i = 0; i < tags.length; i++) {
                var ele = tags[i];
                ele.value = position;
            }
        }
        if($('#positionDisplay').length) {
            $('#positionDisplay').text(position);
        }
        if(size) {
            $('#size').text(size);
        }
        var pos = parsePosition(position);
        if(pos) {
            // fixup external static links on page'

            // Example ensembl link: http://www.ensembl.org/Homo_sapiens/contigview?chr=21&start=33031934&end=33041241
            genomePos.linkFixup(pos, "ensemblLink", new RegExp("(.+start=)[0-9]+"), "end");

            // Example NCBI link: http://www.ncbi.nlm.nih.gov/mapview/maps.cgi?taxid=9606&CHR=21&BEG=33031934&END=33041241
            genomePos.linkFixup(pos, "ncbiLink", new RegExp("(.+BEG=)[0-9]+"), "END");

            // Example medaka link: http://utgenome.org/medakabrowser_ens_jump.php?revision=version1.0&chr=chromosome18&start=14435198&end=14444829
            genomePos.linkFixup(pos, "medakaLink", new RegExp("(.+start=)[0-9]+"), "end");

            if($('#wormbaseLink').length) {
                // e.g. http://www.wormbase.org/db/gb2/gbrowse/c_elegans?name=II:14646301-14667800
                var link = $('#wormbaseLink').attr('href');
                var reg = new RegExp("(.+:)[0-9]+");
                var a = reg.exec(link);
                if(a && a[1]) {
                    $('#wormbaseLink').attr('href', a[1] + pos.start + "-" + pos.end);
                }
            }
            // Fixup DNA link; e.g.: hgc?hgsid=2999470&o=114385768&g=getDna&i=mixed&c=chr7&l=114385768&r=114651696&db=panTro2&hgsid=2999470
            if($('#dnaLink').length) {
                var link = $('#dnaLink').attr('href');
                var reg = new RegExp("(.+&o=)[0-9]+.+&db=[^&]+(.*)");
                var a = reg.exec(link);
                if(a && a[1]) {
                    var url = a[1] + (pos.start - 1) + "&g=getDna&i=mixed&c=" + pos.chrom;
                    url += "&l=" + (pos.start - 1) + "&r=" + pos.end + "&db=" + getDb() + a[2];
                    $('#dnaLink').attr('href', url);
                }
            }
        }
        imageV2.markAsDirtyPage();
    },

    check: function (img, selection)
    {   // return true if user's selection is still w/n the img (including some slop).
        var imgWidth = jQuery(img).width();
        var imgHeight = jQuery(img).height();
        var imgOfs = jQuery(img).offset();
        var slop = 10;

        // We ignore clicks in the gray tab and track title column
        // (we really should suppress all drag activity there,
        // but I don't know how to do that with imgAreaSelect).
        var leftX = hgTracks.revCmplDisp ?  imgOfs.left - slop :
                                            imgOfs.left + hgTracks.insideX - slop;
        var rightX = hgTracks.revCmplDisp ? imgOfs.left + imgWidth - hgTracks.insideX + slop :
                                            imgOfs.left + imgWidth + slop;

        return (   (selection.event.pageX >= leftX)
                && (selection.event.pageX < rightX)
                && (selection.event.pageY >= (imgOfs.top - slop))
                && (selection.event.pageY <  (imgOfs.top + imgHeight + slop)));
    },

    pixelsToBases: function (img, selStart, selEnd, winStart, winEnd)
    {   // Convert image coordinates to chromosome coordinates
        var imgWidth = jQuery(img).width() - hgTracks.insideX;
        var width = hgTracks.winEnd - hgTracks.winStart;
        var mult = width / imgWidth;   // mult is bp/pixel multiplier
        var startDelta;                // startDelta is how many bp's to the right/left
        if(hgTracks.revCmplDisp) {
            var x1 = Math.min(imgWidth, selStart);
            startDelta = Math.floor(mult * (imgWidth - x1));
        } else {
            var x1 = Math.max(hgTracks.insideX, selStart);
            startDelta = Math.floor(mult * (x1 - hgTracks.insideX));
        }
        var endDelta;
        if(hgTracks.revCmplDisp) {
            endDelta = startDelta;
            var x2 = Math.min(imgWidth, selEnd);
            startDelta = Math.floor(mult * (imgWidth - x2));
        } else {
            var x2 = Math.max(hgTracks.insideX, selEnd);
            endDelta = Math.floor(mult * (x2 - hgTracks.insideX));
        }
        var newStart = hgTracks.winStart + startDelta;
        var newEnd = hgTracks.winStart + 1 + endDelta;
        if(newEnd > winEnd) {
            newEnd = winEnd;
        }
        return {chromStart : newStart, chromEnd : newEnd};
    },

    selectionPixelsToBases: function (img, selection)
    {   // Convert selection x1/x2 coordinates to chromStart/chromEnd.
        return genomePos.pixelsToBases(img, selection.x1, selection.x2,
                                        hgTracks.winStart, hgTracks.winEnd);
    },

    update: function (img, selection, singleClick)
    {
        var pos = genomePos.pixelsToBases(img, selection.x1, selection.x2,
                                            hgTracks.winStart, hgTracks.winEnd);
        // singleClick is true when the mouse hasn't moved (or has only moved a small amount).
        if(singleClick) {
            var center = (pos.chromStart + pos.chromEnd)/2;
            pos.chromStart = Math.floor(center - hgTracks.newWinWidth/2);
            pos.chromEnd = pos.chromStart + hgTracks.newWinWidth;
        }
        var newPosition = genomePos.setByCoordinates(hgTracks.chromName,
                                                            pos.chromStart+1, pos.chromEnd);
        return newPosition;
    },

    handleChange: function (response, status)
    {
        var json = eval("(" + response + ")");
        genomePos.set(json.pos);
    },

    changeAssemblies: function (ele)  // UNUSED?  Larry's experimental code
    {   // code to update page when user changes assembly select list.
        $.ajax({
                type: "GET",
                url: "../cgi-bin/hgApi",
                data: "cmd=defaultPos&db=" + getDb(),
                dataType: "html",
                trueSuccess: genomePos.handleChange,
                success: catchErrorOrDispatch,
                error: errorHandler,
                cache: true
            });
        return false;
    }
}

  /////////////////////////////////////
 //// Creating items by dragging /////
/////////////////////////////////////
var makeItemsByDrag = {

    end: function (img, selection)
    {
        var image = $(img);
        var imageId = image.attr('id');
        var trackName = imageId.substring('img_data_'.length);
        var pos = genomePos.selectionPixelsToBases(image, selection);
        var command = document.getElementById('hgt_doJsCommand');
        command.value  = "makeItems " + trackName + " " + hgTracks.chromName;
        command.value +=  " " + pos.chromStart + " " + pos.chromEnd;
        document.TrackHeaderForm.submit();
        return true;
    },

    init: function (trackName)
    {
    // Set up so that they can drag out to define a new item on a makeItems track.
    var img = $("#img_data_" + trackName);
    if(img != undefined && img.length != 0) {
        var imgHeight = imageV2.imgTbl.height();
        jQuery(img.imgAreaSelect( { selectionColor: 'green', outerColor: '',
            minHeight: imgHeight, maxHeight: imgHeight, onSelectEnd: makeItemsByDrag.end,
            autoHide: true, movable: false}));
        }
    },

    load: function ()
    {
        for (var id in hgTracks.trackDb) {
            var rec = hgTracks.trackDb[id];
            if(rec.type != null && rec.type.indexOf("makeItems") == 0) {
                this.init(id);
            }
        }
    }
}

  ///////////////////
 /////  mouse  /////
///////////////////
var mouse = {

    savedOffset: {x:0, y:0},

    saveOffset: function (ev)
    {   // Save the mouse offset associated with this event
        mouse.savedOffset = {x: ev.clientX, y: ev.clientY};
    },

    hasMoved: function (ev)
    {   // return true if mouse has moved a significant amount
        var minPixels = 10;
        var movedX = ev.clientX - mouse.savedOffset.x;
        var movedY = ev.clientY - mouse.savedOffset.y;
        if (arguments.length == 2) {
            var num = Number(arguments[1]);
            if(isNaN(num)) {
                if ( arguments[1].toLowerCase() == "x" )
                    return (movedX > minPixels || movedX < (minPixels * -1));
                if ( arguments[1].toLowerCase() == "y" )
                    return (movedY > minPixels || movedY < (minPixels * -1));
            }
            else
                minPixels = num;
        }
        return (   movedX > minPixels || movedX < (minPixels * -1)
                || movedY > minPixels || movedY < (minPixels * -1));
    }
}

  /////////////////
 //// posting ////
/////////////////
var posting = {

    blockUseMap: false,

    blockMapClicks:   function ()  {         posting.blockUseMap=true;  },
    allowMapClicks:   function ()  {         posting.blockUseMap=false; },
    mapClicksAllowed: function ()  { return (posting.blockUseMap == false); },

    blockTheMapOnMouseMove: function (ev)
    {
        if (!posting.blockUseMap && mouse.hasMoved(ev)) {
            posting.blockUseMap=true;
        }
    },

    mapClk: function ()
    {
        var done = false;
        if(false && imageV2.inPlaceUpdate) {
            // XXXX experimental and only turned on in larrym's tree.
            // Use in-place update if the map item just modifies the current position (this is nice because it's faster
            // and it preserves the users current relative position in the track image).
            //
            // First test handles next/prev item.
            var str = "/cgi-bin/hgTracks\\?position=([^:]+):(.+)&hgsid=(\\d+)&(hgt\.(next|prev)Item=[^&]+)";
            var reg = new RegExp(str);
            var a = reg.exec(this.href);
            if(a && a[1] && a[1] == hgTracks.chromName) {
                imageV2.navigateInPlace("position=" + encodeURIComponent(a[1] + ":" + a[2]) + "&" + a[4], null, true);
                done = true;
            } else {
                // handle next/prev exon
                str = "/cgi-bin/hgTracks\\?position=([^:]+):(.+)&hgsid=(\\d+)$";
                reg = new RegExp(str);
                a = reg.exec(this.href);
                if(a && a[1]) {
                    imageV2.navigateInPlace("position=" + encodeURIComponent(a[1] + ":" + a[2]), null, true);
                    done = true;
                } else {
                    // handle toggle visibility. Request may include a track set, so we cannot use requestImgUpdate.
                    str = "/cgi-bin/hgTracks\\?(position=[^:]+:.+&hgsid=\\d+&([^=]+)=([^&]+))$";
                    reg = new RegExp(str);
                    a = reg.exec(this.href);
                    if(a && a[1]) {
                        imageV2.navigateInPlace(a[1], null, true);
                        // imageV2.requestImgUpdate(a[1], a[1] + "=" + a[2], "", a[2]);
                        done = true;
                    }
                }
            }
        }
        if(done)
            return false;
        else
            return posting.saveSettings(this);
    },

    saveSettings: function (obj)
    {
        if(posting.blockUseMap==true) {
            return false;
        }
        if(obj == undefined || obj.href == undefined) // called directly with obj
            obj = this;                               // and from callback without obj

        if ($(obj).hasClass('noLink'))  // TITLE_BUT_NO_LINK
            return false;

        if (obj.href.match('#') || obj.target.length > 0) {
            //alert("Matched # ["+obj.href+"] or has target:"+obj.target);
            return true;
        }
        var thisForm=$(obj).parents('form');
        if(thisForm == undefined || $(thisForm).length == 0)
            thisForm=$("FORM");
        if($(thisForm).length > 1 )
            thisForm=$(thisForm)[0];
        if(thisForm != undefined && $(thisForm).length == 1) {
            //alert("posting form:"+$(thisForm).attr('name'));
            return postTheForm($(thisForm).attr('name'),obj.href);
        }
        return true;
    }
}

  ///////////////////////////////////////////////
 //// visibility (mixed with group toggle) /////
///////////////////////////////////////////////
var vis = {

    enumOrder: new Array("hide", "dense", "full", "pack", "squish"),  // map cgi enum visibility codes to strings

    update: function (track, visibility)
    {   // Updates visibility state in hgTracks.trackDb and any visible elements on the page.
        // returns true if we modify at least one select in the group list
        var rec = hgTracks.trackDb[track];
        var selectUpdated = false;
        $("select[name=" + escapeJQuerySelectorChars(track) + "]").each(function(t) {
            $(this).attr('class', visibility == 'hide' ? 'hiddenText' : 'normalText');
            $(this).val(visibility);
            selectUpdated = true;
        });
        if(rec) {
            rec.localVisibility = visibility;
        }
        return selectUpdated;
    },

    get: function (track)
    {   // return current visibility for given track
        var rec = hgTracks.trackDb[track];
        if(rec) {
            if(rec.localVisibility) {
                return rec.localVisibility;
            } else {
                return vis.enumOrder[rec.visibility];
            }
        } else {
            return null;
        }
    },

    makeTrackVisible: function (track)
    {
        if(track != null && vis.get(track) != "full") {
            vis.update(track, 'pack');
            $("<input type='hidden' name='" + track + "'value='pack'>").appendTo(
                                                                $(document.TrackHeaderForm));
        }
    },

    toggleForGroup: function (button, prefix)
    {   // toggle visibility of a track group; prefix is the prefix of all the id's of tr's in the
        // relevant group. This code also modifies the corresponding hidden fields and the gif of the +/- img tag.
        imageV2.markAsDirtyPage();
        if(arguments.length > 2)
            return setTableRowVisibility(button, prefix, "hgtgroup", "group",false,arguments[2]);
        else
            return setTableRowVisibility(button, prefix, "hgtgroup", "group",false);
    },

    expandAllGroups: function (newState)
    {   // Set visibility of all track groups to newState (true means expanded).
        // This code also modifies the corresponding hidden fields and the gif's of the +/- img tag.
        imageV2.markAsDirtyPage();
        $(".toggleButton[id$='_button']").each( function (i) {  // works for old img type AND new BUTTONS_BY_CSS
            vis.toggleForGroup(this,this.id.substring(0,this.id.length - 7),newState); // clip '_button' suffix
        });
        return false;
    }

}
  ////////////////////////////////////////////////////////////
 // dragSelect is also known as dragZoom or shift-dragZoom //
////////////////////////////////////////////////////////////
var dragSelect = {

    areaSelector:    null, // formerly "imgAreaSelect". jQuery element used for imgAreaSelect
    autoHideSetting: true, // Current state of imgAreaSelect autoHide setting
    originalCursor:  null,
    startTime:       null,

    rulerModeToggle: function (ele) // UNUSED?
    {
        dragSelect.autoHideSetting = !ele.checked;
        var obj = dragSelect.areaSelector.data('imgAreaSelect');
        obj.setOptions({autoHide : dragSelect.autoHideSetting});
    },

    selectStart: function (img, selection)
    {
        initVars();
        if(rightClick.menu) {
            rightClick.menu.hide();
        }
        var now = new Date();
        dragSelect.startTime = now.getTime();
        posting.blockMapClicks();
    },

    selectChange: function (img, selection)
    {
        if(selection.x1 != selection.x2) {
            if (genomePos.check(img, selection)) {
                genomePos.update(img, selection, false);
                jQuery('body').css('cursor', dragSelect.originalCursor);
            } else {
                jQuery('body').css('cursor', 'not-allowed');
            }
        }
        return true;
    },

    selectEnd: function (img, selection)
    {
        var now = new Date();
        var doIt = false;
        if(dragSelect.originalCursor != null)
            jQuery('body').css('cursor', dragSelect.originalCursor);
        // ignore releases outside of the image rectangle (allowing a 10 pixel slop)
        if(dragSelect.autoHideSetting && genomePos.check(img, selection)) {
        // ignore single clicks that aren't in the top of the image (this happens b/c the clickClipHeight test in dragSelect.selectStart
        // doesn't occur when the user single clicks).
        doIt = dragSelect.startTime != null || selection.y1 <= hgTracks.rulerClickHeight;
        }
        if(doIt) {
            // dragSelect.startTime is null if mouse has never been moved
            var singleClick = (  (selection.x2 == selection.x1)
                              || dragSelect.startTime == null
                              || (now.getTime() - dragSelect.startTime) < 100);
            var newPosition = genomePos.update(img, selection, singleClick);
            if(newPosition != undefined) {
                if(imageV2.inPlaceUpdate) {
                    imageV2.navigateInPlace("position=" + newPosition, null, true);
                } else {
                    jQuery('body').css('cursor', 'wait');
                    document.TrackHeaderForm.submit();
                }
            }
        } else {
            genomePos.revertToOriginalPos();
        }
        dragSelect.startTime = null;
        setTimeout('posting.allowMapClicks();',50); // Necessary incase the dragSelect.selectEnd was over a map item. select takes precedence.
        return true;
    },

    load: function (firstTime)
    {
        var imgHeight = 0;
        if (imageV2.enabled)
            imgHeight = imageV2.imgTbl.height();

        // No longer disable without ruler, because shift-drag still works
        if(typeof(hgTracks) != "undefined") {

            if (hgTracks.rulerClickHeight == undefined || hgTracks.rulerClickHeight == null)
                hgTracks.rulerClickHeight = 0; // will be zero if no ruler track
            var heights = hgTracks.rulerClickHeight;

            dragSelect.areaSelector = jQuery((imageV2.imgTbl).imgAreaSelect({
                selectionColor:  'blue',
                outerColor:      '',
                minHeight:       imgHeight,
                maxHeight:       imgHeight,
                onSelectStart:   dragSelect.selectStart,
                onSelectChange:  dragSelect.selectChange,
                onSelectEnd:     dragSelect.selectEnd,
                autoHide:        dragSelect.autoHideSetting,
                movable:         false,
                clickClipHeight: heights
            }));
        }
    }
}

  /////////////////////////////////////
 //// Chrom Drag/Zoom/Expand code ////
/////////////////////////////////////
jQuery.fn.chromDrag = function(){
this.each(function(){
    // Plan:
    // mouseDown: determine where in map: convert to img location: pxDown
    // mouseMove: flag drag
    // mouseUp: if no drag, then create href centered on bpDown loc with current span
    //          if drag, then create href from bpDown to bpUp
    //          if ctrlKey then expand selection to containing cytoBand(s)
    var img = { top: -1, scrolledTop: -1, height: -1, left: -1, scrolledLeft: -1, width: -1 };  // Image dimensions all in pix
    var chr = { name: "", reverse: false, beg: -1, end: -1, size: -1,
                top: -1, bottom: -1, left: -1, right: -1, width: -1 };   // chrom Dimenaions beg,end,size in bases, rest in pix
    var pxDown = 0;     // pix X location of mouseDown
    var chrImg = $(this);
    var mouseIsDown   = false;
    var mouseHasMoved = false;
    var hilite = jQuery('<div></div>');

    initialize();

    function initialize(){

        findDimensions();

        if(chr.top == -1)
            warn("chromIdeo(): failed to register "+this.id);
        else {
            hiliteSetup();

            $('area.cytoBand').mousedown( function(e)
            {   // mousedown on chrom portion of image only (map items)
                updateImgOffsets();
                pxDown = e.clientX - img.scrolledLeft;
                var pxY = e.clientY - img.scrolledTop;
                if(mouseIsDown == false
                && isWithin(chr.left,pxDown,chr.right) && isWithin(chr.top,pxY,chr.bottom)) {
                    mouseIsDown = true;
                    mouseHasMoved = false;

                    $(document).bind('mousemove',chromMove);
                    $(document).bind( 'mouseup', chromUp);
                    hiliteShow(pxDown,pxDown);
                    return false;
                }
            });
        }
    }

    function chromMove(e)
    {   // If mouse was down, determine if dragged, then show hilite
        if ( mouseIsDown ) {
            var pxX = e.clientX - img.scrolledLeft;
            var relativeX = (pxX - pxDown);
            if(mouseHasMoved || (mouseHasMoved == false && Math.abs(relativeX) > 2)) {
                mouseHasMoved = true;
                if(isWithin(chr.left,pxX,chr.right))
                    hiliteShow(pxDown,pxX);
                else if(pxX < chr.left)
                    hiliteShow(pxDown,chr.left);
                else
                    hiliteShow(pxDown,chr.right);
            }
        }
    }
    function chromUp(e)
    {   // If mouse was down, handle final selection
        $(document).unbind('mousemove',chromMove);
        $(document).unbind('mouseup',chromUp);
        chromMove(e); // Just in case
        if(mouseIsDown) {
            updateImgOffsets();
            var bands;
            var pxUp = e.clientX - img.scrolledLeft;
            var pxY  = e.clientY - img.scrolledTop;
            //warn("chromIdeo("+chr.name+") selected range (pix):"+pxDown+"-"+pxUp+" chrom range (pix):"+chr.left+"-"+chr.right+" chrom range (bp):"+chr.name+":"+chr.beg+"-"+chr.end);
            if(isWithin(0,pxY,img.height)) {  // within vertical range or else cancel
                var selRange = { beg: -1, end: -1, width: -1 };
                var dontAsk = true;

                if(e.ctrlKey) {
                    bands = findCytoBand(pxDown,pxUp);
                    if(bands.end > -1) {
                        pxDown = bands.left;
                        pxUp   = bands.right;
                        mouseHasMoved = true;
                        dontAsk = false;
                        selRange.beg = bands.beg;
                        selRange.end = bands.end;
                        hiliteShow(pxDown,pxUp);
                    }
                }
                else if(mouseHasMoved) {
                    if ( isWithin(-20,pxUp,chr.left) ) // bounded by chrom dimensions: but must remain within image!
                        pxUp = chr.left;
                    if ( isWithin(chr.right,pxUp,img.width + 20) )
                        pxUp = chr.right;

                    if ( isWithin(chr.left,pxUp,chr.right+1) ) {

                        selRange.beg = convertToBases(pxDown);
                        selRange.end = convertToBases(pxUp);

                        if(Math.abs(selRange.end - selRange.beg) < 20)
                            mouseHasMoved = false; // Drag so small: treat as simple click
                        else
                            dontAsk = false;
                    }
                    //else warn("chromIdeo("+chr.name+") NOT WITHIN HORIZONTAL RANGE\n selected range (pix):"+pxDown+"-"+pxUp+" chrom range (pix):"+chr.left+"-"+chr.right);
                }
                if(mouseHasMoved == false) { // Not else because small drag turns this off

                    hiliteShow(pxUp,pxUp);
                    var curWidth = hgTracks.winEnd - hgTracks.winStart;
                    selRange.beg = convertToBases(pxUp) - Math.round(curWidth/2); // Notice that beg is based upon up position
                    selRange.end  = selRange.beg + curWidth;
                }
                if(selRange.end > -1) {
                    // prompt, then submit for new position
                    selRange = rangeNormalizeToChrom(selRange,chr);
                    if(mouseHasMoved == false) { // Update highlight by converting bp back to pix
                        pxDown = convertFromBases(selRange.beg)
                        pxUp = convertFromBases(selRange.end)
                        hiliteShow(pxDown,pxUp);
                    }
                    //if ((selRange.end - selRange.beg) < 50000)
                    //    dontAsk = true;
                    if (dontAsk
                    || confirm("Jump to new position:\n\n"+chr.name+":"+commify(selRange.beg)+
                               "-"+commify(selRange.end)+" size:"+commify(selRange.width)) ) {
                        genomePos.setByCoordinates(chr.name, selRange.beg, selRange.end)
                        $('area.cytoBand').mousedown( function(e) { return false; }); // Stop the presses :0)
                        document.TrackHeaderForm.submit();
                        return true; // Make sure the setTimeout below is not called.
                    }
                }
            }
            //else warn("chromIdeo("+chr.name+") NOT WITHIN VERTICAL RANGE\n selected range (pix):"+pxDown+"-"+pxUp+" chrom range (pix):"+chr.left+"-"+chr.right+"\n cytoTop-Bottom:"+chr.top +"-"+chr.bottom);
            hiliteCancel();
            setTimeout('posting.allowMapClicks();',50);
        }
        mouseIsDown = false;
        mouseHasMoved = false;
    }

    function isWithin(beg,here,end)
    {   // Simple utility
        return ( beg <= here && here < end );
    }
    function convertToBases(pxX)
    {   // Simple utility to convert pix to bases
        var offset = (pxX - chr.left)/chr.width;
        if(chr.reverse)
            offset = 1 - offset;
        return Math.round(offset * chr.size);
    }
    function convertFromBases(bases)
    {   // Simple utility to convert bases to pix
        var offset = bases/chr.size;
        if(chr.reverse)
            offset = 1 - offset;
        return Math.round(offset * chr.width) + chr.left;
    }

    function findDimensions()
    {   // Called at init: determine the dimensions of chrom from 'cytoband' map items
        var lastX = -1;
        $('area.cytoBand').each(function(ix) {
            var loc = this.coords.split(",");
            if(loc.length == 4) {
                var myLeft  = parseInt(loc[0]);
                var myRight = parseInt(loc[2]);
                if (chr.top == -1) {
                    chr.left   = myLeft;
                    chr.right  = myRight;
                    chr.top    = parseInt(loc[1]);
                    chr.bottom = parseInt(loc[3]);
                } else {
                    if (chr.left  > myLeft)
                        chr.left  = myLeft;
                    if (chr.right < parseInt(loc[2]))
                        chr.right = parseInt(loc[2]);
                }

                var range = this.title.substr(this.title.lastIndexOf(':')+1)
                var pos = range.split('-');
                if(pos.length == 2) {
                    if (chr.name.length == 0) {
                        chr.beg = parseInt(pos[0]);
                        //chr.end = parseInt(pos[1]);
                        chr.name = this.title.substring(this.title.lastIndexOf(' ')+1,
                                                        this.title.lastIndexOf(':'))
                    } else {
                        if (chr.beg > parseInt(pos[0]))
                            chr.beg = parseInt(pos[0]);
                    }
                    if (chr.end < parseInt(pos[1])) {
                        chr.end = parseInt(pos[1]);
                        if(lastX == -1)
                            lastX = myRight;
                        else if(lastX > myRight)
                            chr.reverse = true;  // end is advancing, but X is not, so reverse
                    } else if(lastX != -1 && lastX < myRight)
                        chr.reverse = true;      // end is not advancing, but X is, so reverse

                }
                $(this).css( 'cursor', 'text');
                $(this).attr("href","");
            }
        });
        chr.size  = (chr.end   - chr.beg );
        chr.width = (chr.right - chr.left);
    }

    function findCytoBand(pxDown,pxUp)
    {   // Called when mouseup and ctrl: Find the bounding cytoband dimensions, both in pix and bases
        var cyto = { left: -1, right: -1, beg: -1, end: -1 };
        $('area.cytoBand').each(function(ix) {
            var loc = this.coords.split(",");
            if(loc.length == 4) {
                var myLeft  = parseInt(loc[0]);
                var myRight = parseInt(loc[2]);
                if(cyto.left == -1 || cyto.left > myLeft) {
                    if ( isWithin(myLeft,pxDown,myRight) || isWithin(myLeft,pxUp,myRight) ) {
                        cyto.left  = myLeft;
                        var range = this.title.substr(this.title.lastIndexOf(':')+1)
                        var pos = range.split('-');
                        if(pos.length == 2) {
                            cyto.beg  = (chr.reverse ? parseInt(pos[1]) : parseInt(pos[0]));
                        }
                    }
                }
                if(cyto.right == -1 || cyto.right < myRight) {
                    if ( isWithin(myLeft,pxDown,myRight) || isWithin(myLeft,pxUp,myRight) ) {
                        cyto.right = myRight;
                        var range = this.title.substr(this.title.lastIndexOf(':')+1)
                        var pos = range.split('-');
                        if(pos.length == 2) {
                            cyto.end  = (chr.reverse ? parseInt(pos[0]) : parseInt(pos[1]));
                        }
                    }
                }
            }
        });
        return cyto;
    }
    function rangeNormalizeToChrom(selection,chrom)
    {   // Called before presenting or using base range: make sure chrom selection is within chrom range
        if(selection.end < selection.beg) {
            var tmp = selection.end;
            selection.end = selection.beg;
            selection.beg = tmp;
        }
        selection.width = (selection.end - selection.beg);
        selection.beg += 1;
        if (selection.beg < chrom.beg) {
            selection.beg = chrom.beg;
            selection.end = chrom.beg + selection.width;
        }
        if (selection.end > chrom.end) {
            selection.end = chrom.end;
            selection.beg = chrom.end - selection.width;
            if (selection.beg < chrom.beg) { // spans whole chrom
                selection.width = (selection.end - chrom.beg);
                selection.beg = chrom.beg + 1;
            }
        }
        return selection;
    }

    function hiliteShow(down,cur)
    {   // Called on mousemove, mouseup: set drag hilite dimensions
        var topY = img.top;
        var high = img.height;
        var begX = -1;
        var wide = -1;
        if(cur < down) {
            begX = cur + img.left;
            wide = (down - cur);
        } else {
            begX = down + img.left;
            wide = (cur - down);
        }
        $(hilite).css({ left: begX + 'px', width: wide + 'px', top: topY + 'px',
                        height: high + 'px', display:'' });
        $(hilite).show();
    }
    function hiliteCancel(left,width,top,height)
    {   // Called on mouseup: Make green drag hilite disappear when no longer wanted
        $(hilite).hide();
        $(hilite).css({ left: '0px', width: '0px', top: '0px', height: '0px' });
    }

    function hiliteSetup()
    {   // Called on init: setup of drag region hilite (but don't show yet)
        $(hilite).css({ backgroundColor: 'green', opacity: 0.4, borderStyle: 'solid',
                        borderWidth: '1px', bordercolor: '#0000FF' });
        $(hilite).css({ display: 'none', position: 'absolute', overflow: 'hidden', zIndex: 1 });
        jQuery($(chrImg).parents('body')).append($(hilite));
        return hilite;
    }

    function updateImgOffsets()
    {   // Called on mousedown: Gets the current offsets
        var offs = $(chrImg).offset();
        img.top  = Math.round(offs.top );
        img.left = Math.round(offs.left);
        img.scrolledTop  = img.top  - $("body").scrollTop();
        img.scrolledLeft = img.left - $("body").scrollLeft();
        if($.browser.msie) {
            img.height = $(chrImg).outerHeight();
            img.width  = $(chrImg).outerWidth();
        } else {
            img.height = $(chrImg).height();
            img.width  = $(chrImg).width();
        }
        return img;
    }
});
}

  ///////////////////////////
 //// Drag Reorder Code ////
///////////////////////////
var dragReorder = {

    setOrder: function (table)
    {   // Sets the 'order' value for the image table after a drag reorder
        var names = [];
        var values = [];
        $("tr.imgOrd").each(function (i) {
            if ($(this).attr('abbr') != $(this).attr('rowIndex').toString()) {
                $(this).attr('abbr',$(this).attr('rowIndex').toString());
                var name = this.id.substring('tr_'.length) + '_imgOrd';
                names.push(name);
                values.push($(this).attr('abbr'));
            }
        });
        if(names.length > 0) {
            setCartVars(names,values);
            imageV2.markAsDirtyPage();
        }
    },

    showCenterLabel: function (tr, show)
    {   // Will show or hide centerlabel as requested
        // adjust button, sideLabel height, sideLabelOffset and centerlabel display

        if (!$(tr).hasClass('clOpt'))
            return;
        var center = $(tr).find(".sliceDiv.cntrLab");
        if($(center) == undefined)
            return;
        seen = ($(center).css('display') != 'none');
        if(show == seen)
            return;

        var centerHeight = $(center).height();

        var btn = $(tr).find("p.btn");
        var side = $(tr).find(".sliceDiv.sideLab");
        if($(btn) != undefined && $(side) != undefined) {
            var sideImg = $(side).find("img");
            if($(sideImg) != undefined) {
                var top = parseInt($(sideImg).css('top'));
                if(show) {
                    $(btn).css('height',$(btn).height() + centerHeight);
                    $(side).css('height',$(side).height() + centerHeight);
                    top += centerHeight; // top is a negative number
                    $(sideImg).css( {'top': top.toString() + "px" });
                    $( center ).show();
                } else if (!show) {
                    $(btn).css('height',$(btn).height() - centerHeight);
                    $(side).css('height',$(side).height() - centerHeight);
                    top -= centerHeight; // top is a negative number
                    $(sideImg).css( {'top': top.toString() + "px" });
                    $( center ).hide();
                }
            }
        }
    },

    getContiguousRowSet: function (row)
    {   // Returns the set of rows that are of the same class and contiguous
        if(row == null)
            return null;
        var btn = $( row ).find("p.btn");
        if (btn.length == 0)
            return null;
        var classList = $( btn ).attr("class").split(" ");
        var matchClass = classList[0];
        var table = $(row).parents('table#imgTbl')[0];
        var rows = $(table).find('tr');

        // Find start index
        var startIndex = $(row).attr('rowIndex');
        var endIndex = startIndex;
        for(var ix=startIndex-1;ix>=0;ix--) {
            btn = $( rows[ix] ).find("p.btn");
            if (btn.length == 0)
                break;
            classList = $( btn ).attr("class").split(" ");
            if (classList[0] != matchClass)
                break;
            startIndex = ix;
        }

        // Find end index
        for(var ix=endIndex;ix<rows.length;ix++) {
            btn = $( rows[ix] ).find("p.btn");
            if (btn.length == 0)
                break;
            classList = $( btn ).attr("class").split(" ");
            if (classList[0] != matchClass)
                break;
            endIndex = ix;
        }
        return rows.slice(startIndex,endIndex+1); // endIndex is 1 based!
    },

    getCompositeSet: function (row)
    {   // Returns the set of rows that are of the same class and contiguous
        if(row == null)
            return null;
        var rowId = $(row).attr('id').substring('tr_'.length);
        var rec = hgTracks.trackDb[rowId];
        if (tdbIsSubtrack(rec) == false)
            return null;

        var rows = $('tr.trDraggable:has(p.' + rec.parentTrack+')');
        return rows;
    },

    zipButtons: function (table)
    {   // Goes through the image and binds composite track buttons when adjacent
        var rows = $(table).find('tr');
        var lastClass="";
        var lastBtn;
        var lastMatchesLast=false;
        var lastBlue=true;
        var altColors=false;
        var count=0;
        var countN=0;
        for(var ix=0;ix<rows.length;ix++) {    // Need to have buttons in order
            var btn = $( rows[ix] ).find("p.btn");
            if (btn.length == 0)
                continue;
            var classList = $( btn ).attr("class").split(" ");
            var curMatchesLast=(classList[0] == lastClass);

            // centerLabels may be conditionally seen
            if($( rows[ix] ).hasClass('clOpt')) {
                if(curMatchesLast && $( rows[ix - 1] ).hasClass('clOpt'))
                    dragReorder.showCenterLabel(rows[ix],false);  // if same composite and previous is also centerLabel optional then hide center label
                else
                    dragReorder.showCenterLabel(rows[ix],true);
            }

            // On with buttons
            if(lastBtn != undefined) {
                $( lastBtn ).removeClass('btnN btnU btnL btnD');
                if(curMatchesLast && lastMatchesLast) {
                    $( lastBtn ).addClass('btnL');
                } else if(lastMatchesLast) {
                    $( lastBtn ).addClass('btnU');
                } else if(curMatchesLast) {
                    $( lastBtn ).addClass('btnD');
                } else {
                    $( lastBtn ).addClass('btnN');
                    countN++;
                }
                count++;
                if(altColors) {
                    lastBlue = (lastMatchesLast == lastBlue); // lastMatch and lastBlue or not lastMatch and notLastBlue
                    if(lastBlue)    // Too  smart by 1/3rd
                        $( lastBtn ).addClass(    'btnBlue' );
                    else
                        $( lastBtn ).removeClass( 'btnBlue' );
                }
            }
            lastMatchesLast = curMatchesLast;
            lastClass = classList[0];
            lastBtn = btn;
        }
        if(lastBtn != undefined) {
            $( lastBtn ).removeClass('btnN btnU btnL btnD');
            if(lastMatchesLast) {
                $( btn).addClass('btnU');
            } else {
                $( lastBtn ).addClass('btnN');
                countN++;
            }
            if(altColors) {
                    lastBlue = (lastMatchesLast == lastBlue); // lastMatch and lastBlue or not lastMatch and notLastBlue
                    if(lastBlue)    // Too  smart by 1/3rd
                        $( lastBtn ).addClass(    'btnBlue' );
                    else
                        $( lastBtn ).removeClass( 'btnBlue' );
            }
            count++;
        }
        //warn("Zipped "+count+" buttons "+countN+" are independent.");
    },

    dragHandleMouseOver: function ()
    {   // Highlights a single row when mouse over a dragHandle column (sideLabel and buttons)
        if(jQuery.tableDnD == undefined) {
            //var handle = $("td.dragHandle");
            //$(handle)
            //    .unbind('mouseenter')//, jQuery.tableDnD.mousemove);
            //    .unbind('mouseleave');//, jQuery.tableDnD.mouseup);
            return;
        }
        if (jQuery.tableDnD.dragObject == null) {
            $( this ).parents("tr.trDraggable").addClass("trDrag");
        }
    },

    dragHandleMouseOut: function ()
    {   // Ends row highlighting by mouse over
        $( this ).parents("tr.trDraggable").removeClass("trDrag");
    },

    buttonMouseOver: function ()
    {   // Highlights a composite set of buttons, regarless of whether tracks are adjacent
        if(jQuery.tableDnD == undefined || jQuery.tableDnD.dragObject == null) {
            var classList = $( this ).attr("class").split(" ");
            var btns = $( "p." + classList[0] );
            $( btns ).removeClass('btnGrey');
            $( btns ).addClass('btnBlue');
            if (jQuery.tableDnD != undefined) {
                var rows = dragReorder.getContiguousRowSet($(this).parents('tr.trDraggable')[0]);
                if (rows)
                    $( rows ).addClass("trDrag");
            }
        }
    },

    buttonMouseOut: function ()
    {   // Ends composite highlighting by mouse over
        var classList = $( this ).attr("class").split(" ");
        var btns = $( "p." + classList[0] );
        $( btns ).removeClass('btnBlue');
        $( btns ).addClass('btnGrey');
        if (jQuery.tableDnD != undefined) {
            var rows = dragReorder.getContiguousRowSet($(this).parents('tr.trDraggable')[0]);
            if (rows)
            $( rows ).removeClass("trDrag");
        }
    },

    trMouseOver: function (e) // LARRY: is this the right thing?
    {
        // Trying to make sure there is always a imageV2.lastTrack so that we know where we are
        var id = '';
        var a = /tr_(.*)/.exec($(this).attr('id'));  // voodoo
        if(a && a[1]) {
            id = a[1];
        }
        if (id.length > 0) {
            if (imageV2.lastTrack == null || imageV2.lastTrack.id != id)
                imageV2.lastTrack = rightClick.makeMapItem(id); // currentMapItem gets set by mapItemMapOver
        }                                      // This is just backup
    },

    mapItemMouseOver: function ()
    {
        // Record data for current map area item
        var id = this.id;
        if (id == undefined || id == null || id.length == 0) {
            id = '';
            var tr = $( this ).parents('tr.imgOrd');
            if ( $(tr).length == 1 ) {
                var a = /tr_(.*)/.exec($(tr).attr('id'));  // voodoo
                if(a && a[1]) {
                    id = a[1];
                }
            }
        }
        if (id.length > 0) {
            rightClick.currentMapItem = rightClick.makeMapItem(id);
            if(rightClick.currentMapItem != null) {
                rightClick.currentMapItem.href = this.href;
                rightClick.currentMapItem.title = this.title;
            }
        }
    },

    mapItemMouseOut: function ()
    {
        imageV2.lastTrack = rightClick.currentMapItem; // Just a backup
        rightClick.currentMapItem = null;
    },

    init: function ()
    {   // Make side buttons visible (must also be called when updating rows in the imgTbl).
        var btns = $("p.btn");
        if(btns.length > 0) {
            dragReorder.zipButtons($('#imgTbl'));
            $(btns).mouseenter( dragReorder.buttonMouseOver );
            $(btns).mouseleave( dragReorder.buttonMouseOut  );
            $(btns).show();
        }
        var handle = $("td.dragHandle");
        if(handle.length > 0) {
            $(handle).mouseenter( dragReorder.dragHandleMouseOver );
            $(handle).mouseleave( dragReorder.dragHandleMouseOut  );
        }

        // setup mouse callbacks for the area tags
        $(imageV2.imgTbl).find("tr").mouseover( dragReorder.trMouseOver );

        $(".area").each( function(t) {
                            this.onmouseover = dragReorder.mapItemMouseOver;
                            this.onmouseout = dragReorder.mapItemMouseOut;
                            this.onclick = posting.mapClk;
                        });
    }
}


  //////////////////////////
 //// Drag Scroll code ////
//////////////////////////
jQuery.fn.panImages = function(){
    // globals across all panImages
    genomePos.original = genomePos.getOriginalPos();              // XXXX what is this for? (this already happened in initVars).
    var leftLimit   = hgTracks.imgBoxLeftLabel * -1;
    var rightLimit  = (hgTracks.imgBoxPortalWidth - hgTracks.imgBoxWidth + leftLimit);
    var only1xScrolling = ((hgTracks.imgBoxWidth - hgTracks.imgBoxPortalWidth) == 0);//< hgTracks.imgBoxLeftLabel);
    var prevX       = (hgTracks.imgBoxPortalOffsetX + hgTracks.imgBoxLeftLabel) * -1;
    var portalWidth = 0;
    var savedPosition;

    this.each(function(){

    var pic;
    var pan;

    if ( $(this).is("img") ) {
        pan = $(this).parent("div");
        pic = $(this);
    }
    else if ( $(this).is("div.scroller")  ) {
        pan = $(this);
        pic = $(this).children("img#panImg"); // Get the real pic
    }

    if(pan == undefined || pic == undefined) {
        throw "Not a div with a child image! 'panImages' can only be used with divs contain images.";
    }

    // globals across all panImages
    portalWidth     = $(pan).width();
    // globals to one panImage
    var newX        = 0;
    var mouseDownX  = 0;
    var mouseIsDown = false;
    var beyondImage = false;
    var atEdge      = false;

    initialize();

    function initialize(){

        $(pan).parents('td.tdData').mousemove(function(e) {
            if (e.shiftKey)
                $(this).css('cursor',"crosshair");  // shift-dragZoom
            else if ( $.browser.msie )     // IE will override map item cursors if this gets set
                $(this).css('cursor',"");  // normal pointer when not over clickable item
            //else // NOTE: Open hand cursor is being removed because it makes vis toggling less obvious
            //    $(this).css('cursor',"url(../images/grabber.cur),w-resize");  // dragScroll
        });

        panAdjustHeight(prevX);

        pan.mousedown(function(e){
             if (e.which > 1 || e.button > 1 || e.shiftKey)
                 return true;
            if(mouseIsDown == false) {
                if(rightClick.menu) {
                    rightClick.menu.hide();
                }
                mouseIsDown = true;
                mouseDownX = e.clientX;
                atEdge = (!beyondImage && (prevX >= leftLimit || prevX <= rightLimit));
                $(document).bind('mousemove',panner);
                $(document).bind( 'mouseup', panMouseUp);  // Will exec only once
                return false;
            }
        });
    }

    function panner(e) {
        //if (!e) e = window.event;
        if ( mouseIsDown ) {
            var relativeX = (e.clientX - mouseDownX);

            if(relativeX != 0) {
                if (posting.mapClicksAllowed()) {
                    // need to throw up a z-index div.  Wait mask?
                    savedPosition = genomePos.get();
                    dragMaskShow();
                    posting.blockMapClicks();
                }
                var decelerator = 1;
                //var wingSize    = 1000; // 0 stops the scroll at the edges.
                // Remeber that offsetX (prevX) is negative
                newX = prevX + relativeX;
                if ( newX >= leftLimit ) { // scrolled all the way to the left
                    if(atEdge) {  // Do not drag straight off edge.  Force second drag
                        beyondImage = true;
                        newX = leftLimit + (newX - leftLimit)/decelerator;// slower
                        //if (newX >= leftLimit + wingSize) // Don't go too far over the edge!
                        //    newX =  leftLimit + wingSize;
                    } else
                        newX = leftLimit;

                } else if ( newX < rightLimit ) { // scrolled all the way to the right
                    if(atEdge) {  // Do not drag straight off edge.  Force second drag
                        beyondImage = true;
                        newX = rightLimit - (rightLimit - newX)/decelerator;// slower
                        //if (newX < rightLimit - wingSize) // Don't go too far over the edge!
                        //    newX = rightLimit - wingSize;
                    } else
                        newX = rightLimit;

                } else if(newX >= rightLimit && newX < leftLimit)
                    beyondImage = false; // could have scrolled back without mouse up

                newX = panUpdatePosition(newX,true);
                var nowPos = newX.toString() + "px";
                $(".panImg").css( {'left': nowPos });
                $('.tdData').css( {'backgroundPosition': nowPos } );
                if (!only1xScrolling)
                    panAdjustHeight(newX);  // NOTE: This will dynamically resize image while scrolling.  Do we want to?
            }
        }
    }
    function panMouseUp(e) {  // Must be a separate function instead of pan.mouseup event.
        //if (!e) e = window.event;
        if(mouseIsDown) {

            dragMaskClear();
            $(document).unbind('mousemove',panner);
            $(document).unbind('mouseup',panMouseUp);
            mouseIsDown = false;
            setTimeout('posting.allowMapClicks();',50); // Necessary incase the dragSelect.selectEnd was over a map item. select takes precedence.

            // Outside image?  Then abandon.
            var curY = e.pageY;
            var imgTbl = $('#imgTbl');
            var north = $(imgTbl).offset().top;
            var south = north + $(imgTbl).height();
            if (curY < north || curY > south) {
                atEdge = false;
                beyondImage = false;
                if (savedPosition != undefined)
                    genomePos.set(savedPosition,null);
                var oldPos = prevX.toString() + "px";
                $(".panImg").css( {'left': oldPos });
                $('.tdData').css( {'backgroundPosition': oldPos } );
                return true;
            }

            // Do we need to fetch anything?
            if(beyondImage) {
                if(imageV2.inPlaceUpdate) {
                    var pos = parsePosition(genomePos.get());
                    imageV2.navigateInPlace("position=" +
                            encodeURIComponent(pos.chrom + ":" + pos.start + "-" + pos.end),
                            null, true);
                } else {
                    document.TrackHeaderForm.submit();
                }
                return true; // Make sure the setTimeout below is not called.
            }

            // Just a normal scroll within a >1X image
            if(prevX != newX) {
                prevX = newX;
                if (!only1xScrolling) {
                    //panAdjustHeight(newX); // NOTE: This will resize image after scrolling.  Do we want to while scrolling?
                    // This is important, since AJAX could lead to reinit after this within bounds scroll
                    hgTracks.imgBoxPortalOffsetX = (prevX * -1) - hgTracks.imgBoxLeftLabel;
                    hgTracks.imgBoxPortalLeft = newX.toString() + "px";
                }
            }
        }
    }
    });  // end of this.each(function(){

    function panUpdatePosition(newOffsetX,bounded)
    {
        // Updates the 'position/search" display with change due to panning
        var closedPortalStart = hgTracks.imgBoxPortalStart + 1;   // Correction for half open portal coords
        var portalWidthBases = hgTracks.imgBoxPortalEnd - closedPortalStart;
        var portalScrolledX  = hgTracks.imgBoxPortalOffsetX+hgTracks.imgBoxLeftLabel + newOffsetX;
        var recalculate = false;

        var newPortalStart = 0;
        if (hgTracks.revCmplDisp)
            newPortalStart = closedPortalStart +
                                Math.round(portalScrolledX*hgTracks.imgBoxBasesPerPixel); // As offset goes down, so do bases seen.
        else
            newPortalStart = closedPortalStart -
                                Math.round(portalScrolledX*hgTracks.imgBoxBasesPerPixel); // As offset goes down, bases seen goes up!
        if (newPortalStart < hgTracks.chromStart && bounded) {     // Stay within bounds
            newPortalStart = hgTracks.chromStart;
            recalculate = true;
        }
        var newPortalEnd = newPortalStart + portalWidthBases;
        if (newPortalEnd > hgTracks.chromEnd && bounded) {
            newPortalEnd = hgTracks.chromEnd;
            newPortalStart = newPortalEnd - portalWidthBases;
            recalculate = true;
        }
        if(newPortalStart > 0) {
            var newPos = hgTracks.chromName + ":" +
                            commify(newPortalStart) + "-" + commify(newPortalEnd);
            genomePos.set(newPos, 0); // 0 means no need to change the size
        }
        if (recalculate && hgTracks.imgBoxBasesPerPixel > 0) { // Need to recalculate X for bounding drag
            portalScrolledX = (closedPortalStart - newPortalStart) / hgTracks.imgBoxBasesPerPixel;
            newOffsetX = portalScrolledX - (hgTracks.imgBoxPortalOffsetX+hgTracks.imgBoxLeftLabel);
        }
        return newOffsetX;
    }
    function mapTopAndBottom(mapName,east,west)
    {
    // Find the top and bottom px given left and right boundaries
        var mapPortal = { top: -10, bottom: -10 };
        var items = $("map[name='"+mapName+"']").children();
        if ($(items).length>0) {
            $(items).each(function(t) {
                var loc = this.coords.split(",");
                var aleft   = parseInt(loc[0]);
                var aright  = parseInt(loc[2]);
                if(aleft < west && aright >= east) {
                    var atop    = parseInt(loc[1]);
                    var abottom = parseInt(loc[3]);
                    if (mapPortal.top    < 0 ) {
                        mapPortal.top    = atop;
                        mapPortal.bottom = abottom;
                    } else if(mapPortal.top > atop) {
                            mapPortal.top = atop;
                    } else if(mapPortal.bottom < abottom) {
                            mapPortal.bottom = abottom;
                    }
                }
            });
        }
        return mapPortal;
    }
    function panAdjustHeight(newOffsetX) {
        // Adjust the height of the track data images so that bed items scrolled off screen
        // do not waste vertical real estate

        // Is the > 1x?
        if (only1xScrolling)
            return;

        var east = newOffsetX * -1;
        var west = east + portalWidth;
        $(".panImg").each(function(t) {
            var mapid  = this.id.replace('img_','map_');
            var hDiv   = $(this).parent();
            var north  = parseInt($(this).css("top")) * -1;
            var south  = north + $(hDiv).height();

            var mapPortal = mapTopAndBottom(mapid,east,west);
            if(mapPortal.top > 0) {
                var topdif = Math.abs(mapPortal.top - north);
                var botdif = Math.abs(mapPortal.bottom - south);
                if(topdif > 2 || botdif > 2) {
                    $(hDiv).height( mapPortal.bottom - mapPortal.top );
                    north = mapPortal.top * -1;
                    $(this).css( {'top': north.toString() + "px" });

                    // Need to adjust side label height as well!
                    var imgId = this.id.split("_");
                    var titlePx = 0;
                    var center = $("#img_center_"+imgId[2]);
                    if(center.length > 0) {
                        titlePx = $(center).parent().height();
                        north += titlePx;
                    }
                    var side = $("#img_side_"+imgId[2]);
                    if (side.length > 0) {
                        $(side).parent().height( mapPortal.bottom - mapPortal.top + titlePx);
                        $(side).css( {'top': north.toString() + "px" });
                    }
                    var btn = $("#p_btn_"+imgId[2]);
                    if (btn.length > 0) {
                        $(btn).height( mapPortal.bottom - mapPortal.top + titlePx);
                    } else {
                        btn = $("#img_btn_"+imgId[2]);
                        if (btn.length > 0) {
                            $(btn).parent().height( mapPortal.bottom - mapPortal.top + titlePx);
                            $(btn).css( {'top': top.toString() + "px" });
                        }
                    }
                }
            }
        });
        dragMaskResize();  // Resizes the dragMask to match current image size
    }

    function dragMaskShow() {   // Sets up the dragMask to show grabbing cursor within image and not allowed north and south of image

        var imgTbl = $('#imgTbl');
        // Find or create the waitMask (which masks the whole page)
        var dragMask = $('div#dragMask');
        if (dragMask == undefined || dragMask.length == 0) {
            $("body").prepend("<div id='dragMask' class='waitMask'></div>");
            dragMask = $('div#dragMask');
        }

        $('body').css('cursor','not-allowed');
        $(dragMask).css('cursor',"url(../images/grabbing.cur),w-resize");
        $(dragMask).css({opacity:0.0,display:'block',
                        top: $(imgTbl).position().top.toString() + 'px',
                        height: $(imgTbl).height().toString() + 'px' });
        //$(dragMask).css({opacity:0.4,backgroundColor:'gray',zIndex:999}); // temporarily so I can see it
    }

    function dragMaskResize() {   // Resizes dragMask (called when image is dynamically resized in >1x scrolling)

        var imgTbl = $('#imgTbl');
        // Find or create the waitMask (which masks the whole page)
        var dragMask = $('div#dragMask');
        if (dragMask != undefined && dragMask.length >= 1) {
            $(dragMask).height( $(imgTbl).height() );
        }
    }

    function dragMaskClear() {        // Clears the dragMask
        $('body').css('cursor','auto')
        var  dragMask = $('#dragMask');
        if (dragMask != undefined )
            $(dragMask).hide();
    }



};

  ///////////////////////////////////////
 //// rightClick (aka context menu) ////
///////////////////////////////////////
var rightClick = {

    menu: null,
    selectedMenuItem: null,   // currently choosen context menu item (via context menu).
    floatingMenuItem: null,
    currentMapItem:   null,
    supportZoomCodon: false,  // turn on experimental zoom-to-codon functionality (currently only on in larry's tree).

    makeMapItem: function (id)
    {   // Create a dummy mapItem on the fly (for objects that don't have corresponding entry in the map).
        if(id && id.length > 0
        && typeof(hgTracks.trackDb) != "undefined" && hgTracks.trackDb != null) {
            var title;
            var rec = hgTracks.trackDb[id];
            if(rec) {
                title = rec.shortLabel;
            } else {
                title = id;
            }
            return {id: id, title: "configure " + title};
        } else {
            return null;
        }
    },

    findMapItem: function (e)
    {   // Find mapItem for given event; returns item object or null if none found.

        if(rightClick.currentMapItem) {
            return rightClick.currentMapItem;
        }

        // rightClick for non-map items that can be resolved to their parent tr and then trackName (e.g. items in gray bar)
        var tr = $( e.target ).parents('tr.imgOrd');
        if ( $(tr).length == 1 ) {
            var a = /tr_(.*)/.exec($(tr).attr('id'));  // voodoo
            if(a && a[1]) {
                var id = a[1];
                return rightClick.makeMapItem(id);
            }
        }
        return null;
    },

    windowOpenFailedMsg: function ()
    {
        warn("Your web browser prevented us from opening a new window.\n\nPlease change your browser settings to allow pop-up windows from " + document.domain + ".");
    },

    handleZoomCodon: function (response, status)
    {
        var json = eval("(" + response + ")");
        if(json.pos) {
            genomePos.set(json.pos, 3);
            if(document.TrackForm)
                document.TrackForm.submit();
            else
                document.TrackHeaderForm.submit();
        } else {
            alert(json.error);
        }
    },

    handleViewImg: function (response, status)
    {   // handles view image response, which must get new image without imageV2 gimmickery
        jQuery('body').css('cursor', '');
        var str = "<IMG[^>]*SRC='([^']+)'";
        var reg = new RegExp(str);
        a = reg.exec(response);
        if(a && a[1]) {
            if(window.open(a[1]) == null) {
                rightClick.windowOpenFailedMsg();
            }
            return;
        }
        warn("Couldn't parse out img src");
    },

    myPrompt: function (msg, callback)
    {   // replacement for prompt; avoids misleading/confusing security warnings which are caused by prompt in IE 7+
        // callback is called if user presses "OK".
        $("body").append("<div id = 'myPrompt'><div id='dialog' title='Basic dialog'><form>" +
                            msg + "<input id='myPromptText' value=''></form>");
        $("#myPrompt").dialog({
                                modal: true,
                                closeOnEscape: true,
                                buttons: { "OK": function() {
                                                    var myPromptText = $("#myPromptText").val();
                                                    $(this).dialog("close");
                                                    callback(myPromptText);
                                                }
                                        }
                            });
    },

    hit: function (menuItemClicked, menuObject, cmd, args)
    {
        setTimeout( function() {
                        rightClick.hitFinish(menuItemClicked, menuObject, cmd, args);
                    }, 1);
    },

    hitFinish: function (menuItemClicked, menuObject, cmd, args)
    {   // dispatcher for context menu hits
        var id = rightClick.selectedMenuItem.id;
        if(menuObject.shown) {
            // warn("Spinning: menu is still shown");
            setTimeout(function() { rightClick.hitFinish(menuItemClicked, menuObject, cmd); }, 10);
            return;
        }
        if(cmd == 'selectWholeGene' || cmd == 'getDna') {
                // bring whole gene into view or redirect to DNA screen.
                var href = rightClick.selectedMenuItem.href;
                var chromStart, chromEnd;
                var a = /hgg_chrom=(\w+)&/.exec(href);
                // Many links leave out the chrom (b/c it's in the server side cart as "c")
                var chrom = hgTracks.chromName;
                if(a) {
                    if(a && a[1])
                        chrom = a[1];
                    a = /hgg_start=(\d+)/.exec(href);
                    if(a && a[1])
                        chromStart = parseInt(a[1]) + 1;
                    a = /hgg_end=(\d+)/.exec(href);
                    if(a && a[1])
                        chromEnd = parseInt(a[1]);
                } else {
                    // a = /hgc.*\W+c=(\w+)/.exec(href);
                    a = /hgc.*\W+c=(\w+)/.exec(href);
                    if(a && a[1])
                        chrom = a[1];
                    a = /o=(\d+)/.exec(href);
                    if(a && a[1])
                        chromStart = parseInt(a[1]) + 1;
                    a = /t=(\d+)/.exec(href);
                    if(a && a[1])
                        chromEnd = parseInt(a[1]);
                }
                if(chrom == null || chromStart == null || chromEnd == null) {
                    warn("couldn't parse out genomic coordinates");
                } else {
                    if(cmd == 'getDna')
                    {
                        if(window.open("../cgi-bin/hgc?g=getDna&i=mixed&c=" +
                                        chrom + "&l=" + (chromStart - 1) + "&r=" + chromEnd
                                      ) == null) {
                            rightClick.windowOpenFailedMsg();
                        }
                    } else {
                        var newPosition = genomePos.setByCoordinates(chrom, chromStart, chromEnd);
                        var reg = new RegExp("hgg_gene=([^&]+)");
                        var a = reg.exec(href);
                        var name;
                        // pull item name out of the url so we can set hgFind.matches (redmine 3062)
                        if(a && a[1]) {
                            name = a[1];
                        } else {
                            reg = new RegExp("[&?]i=([^&]+)");
                            a = reg.exec(href);
                            if(a && a[1]) {
                                name = a[1];
                            }
                        }
                        if(imageV2.inPlaceUpdate) {
                            // XXXX This attempt to "update whole track image in place" didn't work for a variety of reasons
                            // (e.g. safari doesn't parse map when we update on the client side), so this is currently dead code.
                            // However, this now works in all other browsers, so we may turn this on for non-safari browsers
                            // (see redmine #4667).
                            jQuery('body').css('cursor', '');
                            var data = "hgt.trackImgOnly=1&hgt.ideogramToo=1&position=" +
                                       newPosition + "&hgsid=" + getHgsid();
                            if(name)
                                data += "&hgFind.matches=" + name;
                            $.ajax({
                                    type: "GET",
                                    url: "../cgi-bin/hgTracks",
                                    data: data,
                                    dataType: "html",
                                    trueSuccess: imageV2.updateImgAndMap,
                                    success: catchErrorOrDispatch,
                                    error: errorHandler,
                                    cmd: cmd,
                                    loadingId: showLoadingImage("imgTbl"),
                                    cache: false
                                });
                        } else {
                            // do a full page refresh to update hgTracks image
                            jQuery('body').css('cursor', 'wait');
                            var ele;
                            if(document.TrackForm)
                                ele = document.TrackForm;
                            else
                                ele = document.TrackHeaderForm;
                            if(name)
                                $(ele).append("<input type='hidden' name='hgFind.matches' " +
                                              "value='" + name + "'>");
                            ele.submit();
                        }
                    }
                }
        } else if (cmd == 'zoomCodon' || cmd == 'zoomExon') {
            var num, ajaxCmd, msg;
            if(cmd == 'zoomCodon') {
                msg = "Please enter the codon number to jump to:";
                ajaxCmd = 'codonToPos';
            } else {
                msg = "Please enter the exon number to jump to:";
                ajaxCmd = 'exonToPos';
            }
            rightClick.myPrompt(msg, function(results) {
                $.ajax({
                        type: "GET",
                        url: "../cgi-bin/hgApi",
                        data: "db=" + getDb() +  "&cmd=" + ajaxCmd + "&num=" + results +
                              "&table=" + args.table + "&name=" + args.name,
                        trueSuccess: rightClick.handleZoomCodon,
                        success: catchErrorOrDispatch,
                        error: errorHandler,
                        cache: true
                    });
                    });
        } else if (cmd == 'hgTrackUi_popup') {

            popUp.hgTrackUi( rightClick.selectedMenuItem.id, false );  // Launches the popup but shields the ajax with a waitOnFunction

        } else if (cmd == 'hgTrackUi_follow') {

            var url = "hgTrackUi?hgsid=" + getHgsid() + "&g=";
            var rec = hgTracks.trackDb[id];
            if (tdbHasParent(rec) && tdbIsLeaf(rec))
                url += rec.parentTrack
            else {
                var link = $( 'td#td_btn_'+ rightClick.selectedMenuItem.id ).children('a'); // The button already has the ref
                if ($(link) != undefined)
                    url = $(link).attr('href');
                else
                    url += rightClick.selectedMenuItem.id;
            }
            location.assign(url);

        } else if (cmd == 'dragZoomMode') {
            dragSelect.autoHideSetting = true;
            var obj = dragSelect.areaSelector.data('imgAreaSelect');
            obj.setOptions({autoHide : true, movable: false});
        } else if (cmd == 'hilightMode') {
            dragSelect.autoHideSetting = false;
            var obj = dragSelect.areaSelector.data('imgAreaSelect');
            obj.setOptions({autoHide : false, movable: true});
        } else if (cmd == 'viewImg') {
            // Fetch a new copy of track img and show it to the user in another window. This code assume we have updated
            // remote cart with all relevant chages (e.g. drag-reorder).
/* Here's how to do this more directly with hgRenderTracks:
            if(window.open("../cgi-bin/hgRenderTracks?hgt.internal=1&hgsid=" + getHgsid()) == null) {
                rightClick.windowOpenFailedMsg();
            }
            return;
*/
            var data = "hgt.imageV1=1&hgt.trackImgOnly=1&hgsid=" + getHgsid();
            jQuery('body').css('cursor', 'wait');
            $.ajax({
                    type: "GET",
                    url: "../cgi-bin/hgTracks",
                    data: data,
                    dataType: "html",
                    trueSuccess: rightClick.handleViewImg,
                    success: catchErrorOrDispatch,
                    error: errorHandler,
                    cmd: cmd,
                    cache: false
                });
        } else if (cmd == 'openLink' || cmd == 'followLink') {
            var href = rightClick.selectedMenuItem.href;
            var vars = new Array("c", "l", "r", "db");
            var valNames = new Array("chromName", "winStart", "winEnd");
            for (var i in vars) {
                // make sure the link contains chrom and window width info (necessary b/c we are stripping hgsid and/or the cart may be empty);
                // but don't add chrom to wikiTrack links (see redmine #2476).
                var v = vars[i];
                var val;
                if(v == "db") {
                    val = getDb();
                } else {
                    val = hgTracks[valNames[i]];
                }
                if (val
                && id != "wikiTrack"
                && (href.indexOf("?" + v + "=") == -1)
                && (href.indexOf("&" + v + "=") == -1)) {
                    href = href + "&" + v + "=" + val;
                }
            }
            if(cmd == 'followLink') {
                // XXXX This is blocked by Safari's popup blocker (without any warning message).
                location.assign(href);
            } else {
                // Remove hgsid to force a new session (see redmine ticket 1333).
                href = removeHgsid(href);
                if(window.open(href) == null) {
                    rightClick.windowOpenFailedMsg();
                }
            }
        } else if (cmd == 'float') {
            if(rightClick.floatingMenuItem && rightClick.floatingMenuItem == id) {
                $.floatMgr.FOArray = new Array();
                rightClick.floatingMenuItem = null;
            } else {
                if(rightClick.floatingMenuItem) {
                    // This doesn't work.
                    $('#img_data_' + rightClick.floatingMenuItem).parent().restartFloat();
                    // This does work
                    $.floatMgr.FOArray = new Array();
                }
                rightClick.floatingMenuItem = id;
                rightClick.reloadFloatingItem();
                imageV2.requestImgUpdate(id, "hgt.transparentImage=0", "");
            }
        } else if (cmd == 'hideSet') {
            var row = $( 'tr#tr_' + id );
            var rows = dragReorder.getContiguousRowSet(row);
            if (rows && rows.length > 0) {
                var vars = new Array();
                var vals = new Array();
                for (var ix=rows.length - 1; ix >= 0; ix--) { // from bottom, just in case remove screws with us
                    var rowId = $(rows[ix]).attr('id').substring('tr_'.length);
                    //if (tdbIsSubtrack(hgTracks.trackDb[rowId]) == false)
                    //    warn('What went wrong?');

                    vars.push(rowId, rowId+'_sel'); // Remove subtrack level vis and explicitly uncheck.
                    vals.push('[]', 0);
                    $(rows[ix]).remove();
                }
                if (vars.length > 0) {
                    setCartVars( vars, vals );
                    dragReorder.init();
                    dragSelect.load(false);
                }
                imageV2.markAsDirtyPage();
            }
        } else if (cmd == 'hideComposite') {
            var rec = hgTracks.trackDb[id];
            if (tdbIsSubtrack(rec)) {
                var row = $( 'tr#tr_' + id );
                var rows = dragReorder.getCompositeSet(row);
                if (rows && rows.length > 0) {
                    for (var ix=rows.length - 1; ix >= 0; ix--) { // from bottom, just in case remove screws with us
                        $(rows[ix]).remove();
                    }
                var selectUpdated = vis.update(rec.parentTrack, 'hide');
                setCartVar(rec.parentTrack, 'hide' );
                dragReorder.init();
                dragSelect.load(false);
                imageV2.markAsDirtyPage();
                }
            }
            //else
            //    warn('What went wrong?');
        } else {   // if ( cmd in 'hide','dense','squish','pack','full','show' )
            // Change visibility settings:
            //
            // First change the select on our form:
            var rec = hgTracks.trackDb[id];
            var selectUpdated = vis.update(id, cmd);

            // Now change the track image
            if(imageV2.enabled && cmd == 'hide')
            {
                // Hide local display of this track and update server side cart.
                // Subtracks controlled by 2 settings so del vis and set sel=0.  Others, just set vis hide.
                if(tdbIsSubtrack(rec))
                    setCartVars( [ id, id+"_sel" ], [ '[]', 0 ] ); // Remove subtrack level vis and explicitly uncheck.
                else if(tdbIsFolderContent(rec))
                    setCartVars( [ id, id+"_sel" ], [ 'hide', 0 ] ); // supertrack children need to have _sel set to trigger superttrack reshaping
                else
                    setCartVar(id, 'hide' );
                $(document.getElementById('tr_' + id)).remove();
                dragReorder.init();
                dragSelect.load(false);
                imageV2.markAsDirtyPage();
            } else if (!imageV2.mapIsUpdateable) {
                jQuery('body').css('cursor', 'wait');
                if(selectUpdated) {
                    // assert(document.TrackForm);
                    document.TrackForm.submit();
                } else {
                        // add a hidden with new visibility value
                        var form = $(document.TrackHeaderForm);
                        $("<input type='hidden' name='"+id+"'value='"+cmd+"'>").appendTo(form);
                        document.TrackHeaderForm.submit();
                }
            } else {
                imageV2.requestImgUpdate(id, id + "=" + cmd, "", cmd);
            }
        }
    },

    makeHitCallback: function (title)
    {   // stub to avoid problem with a function closure w/n a loop
        return function(menuItemClicked, menuObject) {
            rightClick.hit(menuItemClicked, menuObject, title); return true;
        };
    },

    reloadFloatingItem: function ()
    {   // currently dead (experimental code)
        if(rightClick.floatingMenuItem) {
            $('#img_data_' + rightClick.floatingMenuItem).parent().makeFloat(
                {x:"current",y:"current", speed: 'fast', alwaysVisible: true, alwaysTop: true});
        }
    },

    makeImgTag: function (img)
    {   // Return img tag with explicit dimensions for img (dimensions are currently hardwired).
        // This fixes the "weird shadow problem when first loading the right-click menu" seen in FireFox 3.X,
        // which occurred b/c FF doesn't actually fetch the image until the menu is being shown.
        return "<img style='width:16px; height:16px; border-style:none;' src='../images/" +
                img + "' />";
    },


    load: function (img)
    {
        rightClick.menu = img.contextMenu(
            function() {
                popUp.cleanup();   // Popup box is not getting closed properly so must do it here
                if (rightClick.selectedMenuItem == null)  // This is literally an edge case so ignore
                    return;

                var menu = [];
                var selectedImg = rightClick.makeImgTag("greenChecksm.png");
                var blankImg    = rightClick.makeImgTag("invisible16.png");
                var done = false;
                if(rightClick.selectedMenuItem && rightClick.selectedMenuItem.id != null) {
                    var href = rightClick.selectedMenuItem.href;
                    var isHgc, isGene;
                    if(href) {
                        isGene = href.match("hgGene");
                        isHgc = href.match("hgc");
                    }
                    var id = rightClick.selectedMenuItem.id;
                    var rec = hgTracks.trackDb[id];
                    var offerHideSubset    = false;
                    var offerHideComposite = false;
                    var offerSingles       = true;
                    var row = $( 'tr#tr_' + id );
                    if (row) {
                        var btn = $(row).find('p.btnBlue');  // btnBlue means cursor over left button
                        if (btn.length == 1) {
                            var compositeSet = dragReorder.getCompositeSet(row);
                            if (compositeSet && compositeSet.length > 0) {  // There is a composite set
                                offerHideComposite = true;
                                $( compositeSet ).find('p.btn').addClass('blueButtons');  // blue persists

                                var subSet = dragReorder.getContiguousRowSet(row);
                                if (subSet && subSet.length > 1) {
                                    offerSingles = false;
                                    if(subSet.length < compositeSet.length) {
                                        offerHideSubset = true;
                                        $( subSet ).addClass("greenRows"); // green persists
                                    }
                                }
                            }
                        }
                    }

                    // First option is hide sets
                    if (offerHideComposite) {
                        if (offerHideSubset) {
                            var o = new Object();
                            o[blankImg + " hide track subset (green)"] = {
                                                onclick: rightClick.makeHitCallback('hideSet')};
                            //o[rightClick.makeImgTag("highliteGreenX.png") + " hide track subset"] = {onclick: rightClick.makeHitCallback('hideSet')};
                            menu.push(o);
                        }

                        var o = new Object();
                        var str = blankImg + " hide track set";
                        if (offerHideSubset)
                            str += " (blue)";
                        o[str] = {onclick: rightClick.makeHitCallback('hideComposite')};
                        //o[rightClick.makeImgTag("btnBlueX.png") + " hide track set"] = {onclick: rightClick.makeHitCallback('hideComposite')};
                        menu.push(o);
                    }

                    // Second set of options: visibility for single track
                    if (offerSingles) {
                        if (offerHideComposite)
                            menu.push($.contextMenu.separator);

                        // XXXX what if select is not available (b/c trackControlsOnMain is off)?
                        // Move functionality to a hidden variable?
                        var select = $("select[name=" + escapeJQuerySelectorChars(id) + "]");
                        if (select.length > 1)  // Not really needed if $('#hgTrackUiDialog').html(""); has worked
                            select =  [ $(select)[0] ];
                        var cur = $(select).val();
                        if(cur) {
                            $(select).children().each(function(index, o) {
                                var title = $(this).val();
                                var str = blankImg + " " + title;
                                if(title == cur)
                                    str = selectedImg + " " + title;
                                var o = new Object();
                                o[str] = {onclick: function (menuItemClicked, menuObject) {
                                            rightClick.hit(menuItemClicked, menuObject, title);
                                            return true;}};
                                menu.push(o);
                            });
                            done = true;
                        } else {
                            if(rec) {
                                // XXXX check current state from a hidden variable.
                                var visStrings = new Array("hide","dense","squish","pack","full");
                                for (var i in visStrings) {
                                    // XXXX use maxVisibility and change hgTracks so it can hide subtracks
                                    var o = new Object();
                                    var str = blankImg + " " + visStrings[i];
                                    if (rec.canPack
                                    || (visStrings[i] != "pack" && visStrings[i] != "squish")) {
                                        if(rec.localVisibility) {
                                            if(visStrings[i] == rec.localVisibility) {
                                                str = selectedImg + " " + visStrings[i];
                                            }
                                        } else if(visStrings[i] == vis.enumOrder[rec.visibility]) {
                                            str = selectedImg + " " + visStrings[i];
                                        }
                                        o[str] = { onclick:
                                                    rightClick.makeHitCallback(visStrings[i])
                                                 };
                                        menu.push(o);
                                    }
                                }
                                done = true;
                            }
                        }
                    }

                    if(done) {
                        var o = new Object();
                        var any = false;
                        var title = rightClick.selectedMenuItem.title || "feature";
                        var maxLength = 60;
                        if(title.length > maxLength) {
                            title = title.substring(0, maxLength) + "...";
                        }
                        if(isGene || isHgc || id == "wikiTrack") {
                            // Add "Open details..." item
                            var displayItemFunctions = false;
                            if(rec) {
                                if(rec.type.indexOf("wig") == 0 || rec.type.indexOf("bigWig") == 0 || id == "wikiTrack") {
                                    displayItemFunctions = false;
                                } else if(rec.type.indexOf("expRatio") == 0) {
                                    displayItemFunctions = title != "zoomInMore";
                                } else {
                                    displayItemFunctions = true;
                                }
                            }
                            if(displayItemFunctions) {
                                o[rightClick.makeImgTag("magnify.png") + " Zoom to " +  title] = {
                                    onclick: function(menuItemClicked, menuObject) {
                                                rightClick.hit(menuItemClicked, menuObject,
                                                        "selectWholeGene"); return true;
                                              }
                                    };
                                if(rightClick.supportZoomCodon && rec.type.indexOf("genePred") != -1) {
                                    // http://hgwdev-larrym.cse.ucsc.edu/cgi-bin/hgGene?hgg_gene=uc003tqk.2&hgg_prot=P00533&hgg_chrom=chr7&hgg_start=55086724&hgg_end=55275030&hgg_type=knownGene&db=hg19&c=chr7
                                    var name, table;
                                    var reg = new RegExp("hgg_gene=([^&]+)");
                                    var a = reg.exec(href);
                                    if(a && a[1]) {
                                        name = a[1];
                                        reg = new RegExp("hgg_type=([^&]+)");
                                        a = reg.exec(href);
                                        if(a && a[1]) {
                                            table = a[1];
                                        }
                                    } else {
                                        // http://hgwdev-larrym.cse.ucsc.edu/cgi-bin/hgc?o=55086724&t=55275031&g=refGene&i=NM_005228&c=chr7
                                        // http://hgwdev-larrym.cse.ucsc.edu/cgi-bin/hgc?o=55086713&t=55270769&g=wgEncodeGencodeManualV4&i=ENST00000455089&c=chr7
                                        var reg = new RegExp("i=([^&]+)");
                                        var a = reg.exec(href);
                                        if(a && a[1]) {
                                            name = a[1];
                                            reg = new RegExp("g=([^&]+)");
                                            a = reg.exec(href);
                                            if(a && a[1]) {
                                                table = a[1];
                                            }
                                        }
                                    }
                                    if(name && table) {
                                        o[rightClick.makeImgTag("magnify.png")+" Zoom to codon"] =
                                        {   onclick: function(menuItemClicked, menuObject) {
                                                rightClick.hit(menuItemClicked, menuObject,
                                                            "zoomCodon",
                                                            {name: name, table: table});
                                                return true;}
                                        };
                                        o[rightClick.makeImgTag("magnify.png")+" Zoom to exon"] = {
                                            onclick: function(menuItemClicked, menuObject) {
                                                rightClick.hit(menuItemClicked, menuObject,
                                                              "zoomExon",
                                                              {name: name, table: table});
                                                return true; }
                                        };
                                    }
                                }
                                o[rightClick.makeImgTag("dnaIcon.png")+" Get DNA for "+title] = {
                                    onclick: function(menuItemClicked, menuObject) {
                                        rightClick.hit(menuItemClicked, menuObject, "getDna");
                                        return true; }
                                };
                            }
                            o[rightClick.makeImgTag("bookOut.png")+
                                                    " Open details page in new window..."] = {
                                onclick: function(menuItemClicked, menuObject) {
                                    rightClick.hit(menuItemClicked, menuObject, "openLink");
                                    return true; }
                            };
                            any = true;
                        }
                        if(href != undefined && href.length  > 0) {
                            // Add "Show details..." item
                            if(title.indexOf("Click to alter ") == 0) {
                                ; // suppress the "Click to alter..." items
                            } else if(rightClick.selectedMenuItem.href.indexOf("cgi-bin/hgTracks") != -1) {
                                ; // suppress menu items for hgTracks links (e.g. Next/Prev map items).
                            } else {
                                var item;
                                if(title == "zoomInMore")
                                    // avoid showing menu item that says "Show details for zoomInMore..." (redmine 2447)
                                    item = rightClick.makeImgTag("book.png") + " Show details...";
                                else
                                    item = rightClick.makeImgTag("book.png")+" Show details for "+
                                           title + "...";
                                o[item] = {onclick: function(menuItemClicked, menuObject) {
                                           rightClick.hit(menuItemClicked,menuObject,"followLink");
                                           return true; }
                                };
                                any = true;
                            }
                        }
                        if(any) {
                            menu.push($.contextMenu.separator);
                            menu.push(o);
                        }
                    }
                }
                if (!done) {
                    if(false) {
                        // Currently toggling b/n drag-and-zoom mode and hilite mode is disabled b/c we don't know how to keep hilite mode from disabling the
                        // context menus.
                        var o = new Object();
                        var str = "drag-and-zoom mode";
                        if(dragSelect.autoHideSetting) {
                            str += selectedImg;
                            // menu[str].className = 'context-menu-checked-item';
                        }
                        o[str] = { onclick: function(menuItemClicked, menuObject) {
                                     rightClick.hit(menuItemClicked, menuObject, "dragZoomMode");
                                     return true; }
                                 };
                        menu.push(o);
                        o = new Object();
                        // console.dir(ele);
                        str = "hilight mode";
                        if (!dragSelect.autoHideSetting) {
                            str += selectedImg;
                        }
                        o[str] = { onclick: function(menuItemClicked, menuObject) {
                                      rightClick.hit(menuItemClicked, menuObject, "hilightMode");
                                      return true; }
                                 };
                        menu.push(o);
                    }
                    //menu.push({"view image": {onclick: function(menuItemClicked, menuObject) { rightClick.hit(menuItemClicked, menuObject, "viewImg"); return true; }}});
                }

                if(rightClick.selectedMenuItem && rec) {
                    // Add cfg options at just shy of end...
                    var o = new Object();
                    if(tdbIsLeaf(rec)) {

                        if (rec["configureBy"] != 'none'
                        && (!tdbIsCompositeSubtrack(rec) || rec["configureBy"] != 'clickThrough')) {
                            // Note that subtracks never do clickThrough because
                            // parentTrack cfg is the desired clickThrough
                            o[rightClick.makeImgTag("wrench.png")+" Configure "+rec.shortLabel] = {
                                onclick: function(menuItemClicked, menuObject) {
                                    rightClick.hit(menuItemClicked, menuObject, "hgTrackUi_popup");
                                    return true; }
                            };
                        }
                        if(rec.parentTrack != undefined) {
                            o[rightClick.makeImgTag("folderWrench.png")+" Configure "+
                              rec.parentLabel + " track set..."] = {
                                onclick: function(menuItemClicked, menuObject) {
                                    rightClick.hit(menuItemClicked,menuObject,"hgTrackUi_follow");
                                    return true; }
                              };
                        }
                    } else {

                        o[rightClick.makeImgTag("folderWrench.png")+" Configure "+rec.shortLabel +
                          " track set..."] = {
                            onclick: function(menuItemClicked, menuObject) {
                                rightClick.hit(menuItemClicked, menuObject, "hgTrackUi_follow");
                                return true; }
                          };
                    }
                    if(jQuery.floatMgr) {
                        o[(rightClick.selectedMenuItem.id == rightClick.floatingMenuItem ?
                                selectedImg : blankImg) + " float"] = {
                            onclick: function(menuItemClicked, menuObject) {
                                rightClick.hit(menuItemClicked, menuObject, "float");
                                return true; }
                        };
                    }
                    menu.push($.contextMenu.separator);
                    menu.push(o);
                }

                // Add view image at end
                var o = new Object();
                o[rightClick.makeImgTag("eye.png") + " View image"] = {
                    onclick: function(menuItemClicked, menuObject) {
                        rightClick.hit(menuItemClicked, menuObject, "viewImg");
                        return true; }
                };
                menu.push($.contextMenu.separator);
                menu.push(o);

                return menu;
            },
            {
                beforeShow: function(e) {
                    // console.log(mapItems[rightClick.selectedMenuItem]);
                    rightClick.selectedMenuItem = rightClick.findMapItem(e);
                    // XXXX? posting.blockUseMap = true;
                    return true;
                },
                hideTransition:'hide', // hideCallback fails if these are not defined.
                hideSpeed:10,
                hideCallback: function() {
                    $('p.btn.blueButtons').removeClass('blueButtons');
                    $('tr.trDraggable.greenRows').removeClass('greenRows');
                }
            });
        return;
    }

}

  //////////////////////////////////
 //// popup (aka modal dialog) ////
//////////////////////////////////
var popUp = {

    trackName:            "",
    trackDescriptionOnly: false,
    saveAllVars:          null,

    cleanup: function ()
    {  // Clean out the popup box on close
        if ($('#hgTrackUiDialog').html().length > 0 ) {
            $('#hgTrackUiDialog').html("");  // clear out html after close to prevent problems caused by duplicate html elements
            popUp.trackName = ""; //set to defaults
            popUp.trackDescriptionOnly = false;
            popUp.saveAllVars = null;
        }
    },

    _uiDialigRequest: function (trackName,descriptionOnly)
    { // popup cfg dialog
        popUp.trackName = trackName;
        var myLink = "../cgi-bin/hgTrackUi?g=" + trackName + "&hgsid=" + getHgsid() +
                     "&db=" + getDb();
        popUp.trackDescriptionOnly = descriptionOnly;
        if(popUp.trackDescriptionOnly)
            myLink += "&descriptionOnly=1";

        var rec = hgTracks.trackDb[trackName];
        if (!descriptionOnly && rec != null && rec["configureBy"] != null) {
            if (rec["configureBy"] == 'none')
                return;
            else if (rec["configureBy"] == 'clickThrough') {
                jQuery('body').css('cursor', 'wait');
                window.location = myLink;
                return;
            }  // default falls through to configureBy popup
        }
        myLink += "&ajax=1";
        $.ajax({
                    type: "GET",
                    url: myLink,
                    dataType: "html",
                    trueSuccess: popUp.uiDialog,
                    success: catchErrorOrDispatch,
                    error: errorHandler,
                    cmd: rightClick.selectedMenuItem,
                    cache: false
                });
    },

    hgTrackUi: function (trackName,descriptionOnly)
    {
        waitOnFunction( popUp._uiDialigRequest, trackName, descriptionOnly );  // Launches the popup but shields the ajax with a waitOnFunction
    },

    uiDialogOk: function (popObj, trackName)
    {   // When hgTrackUi Cfg popup closes with ok, then update cart and refresh parts of page
        var rec = hgTracks.trackDb[trackName];
        var subtrack = tdbIsSubtrack(rec) ? trackName :undefined;  // If subtrack then vis rules differ
        var allVars = getAllVars($('#hgTrackUiDialog'), subtrack );// For unknown reasons IE8 fails to find $('#pop'), occasionally
        var changedVars = varHashChanges(allVars,popUp.saveAllVars);
        //warn("cfgVars:"+varHashToQueryString(changedVars));
        var newVis = changedVars[trackName];
        var hide = (newVis != null && (newVis == 'hide' || newVis == '[]'));  // subtracks do not have "hide", thus '[]'
        if($('#imgTbl') == undefined) { // On findTracks or config page
            setVarsFromHash(changedVars);
            //if(hide) // TODO: When findTracks or config page has cfg popup, then vis change needs to be handled in page here
        }
        else {  // On image page
            if(hide) {
                setVarsFromHash(changedVars);
                $(document.getElementById('tr_' + trackName)).remove();
                dragReorder.init();
                dragSelect.load(false);
            } else {
                // Keep local state in sync if user changed visibility
                if(newVis != null) {
                    vis.update(trackName, newVis);
                }
                var urlData = varHashToQueryString(changedVars);
                if(urlData.length > 0) {
                    if(imageV2.mapIsUpdateable) {
                        imageV2.requestImgUpdate(trackName,urlData,"");
                    } else {
                        window.location = "../cgi-bin/hgTracks?" + urlData +
                                          "&hgsid=" + getHgsid();
                    }
                }
            }
        }
    },

    uiDialog: function (response, status)
    {
    // Take html from hgTrackUi and put it up as a modal dialog.

        // make sure all links (e.g. help links) open up in a new window
        response = response.replace(/<a /ig, "<a target='_blank' ");

        var cleanHtml = response;
        cleanHtml = stripJsFiles(cleanHtml,true);   // DEBUG msg with true
        cleanHtml = stripCssFiles(cleanHtml,true);  // DEBUG msg with true
        cleanHtml = stripJsEmbedded(cleanHtml,true);// DEBUG msg with true
        $('#hgTrackUiDialog').html("<div id='pop' style='font-size:.9em;'>"+ cleanHtml +"</div>");

        // Strategy for poups with js:
        // - jsFiles and CSS should not be included in html.  Here they are shluped out.
        // - The resulting files ought to be loadable dynamically (with getScript()), but this was not working nicely with the modal dialog
        //   Therefore include files must be included with hgTracks CGI !
        // - embedded js should not be in the popup box.
        // - Somethings should be in a popup.ready() function, and this is emulated below, as soon as the cleanHtml is added
        //   Since there are many possible popup cfg dialogs, the ready should be all inclusive.

        if ( ! popUp.trackDescriptionOnly ) {
            var subtrack = tdbIsSubtrack(hgTracks.trackDb[popUp.trackName]) ? popUp.trackName :"";  // If subtrack then vis rules differ
            popUp.saveAllVars = getAllVars( $('#hgTrackUiDialog'), subtrack );  // Saves the vars that may get changed by the popup cfg.

            // -- popup.ready() -- Here is the place to do things that might otherwise go into a $('#pop').ready() routine!
        }

        // Searching for some selblance of size suitability
        var popMaxHeight = ($(window).height() - 40);
        var popMaxWidth  = ($(window).width() - 40);
        var popWidth     = 740;
        if (popWidth > popMaxWidth)
            popWidth > popMaxWidth;

        $('#hgTrackUiDialog').dialog({
                                ajaxOptions: {
                                    // This doesn't work
                                    cache: true
                                },
                                resizable: true,
                                height: (popUp.trackDescriptionOnly ? popMaxHeight : 'auto'), // Let description scroll vertically
                                width: popWidth,
                                minHeight: 200,
                                minWidth: 700,
                                maxHeight: popMaxHeight,
                                maxWidth: popMaxWidth,
                                modal: true,
                                closeOnEscape: true,
                                autoOpen: false,
                                buttons: { "OK": function() {
                                        if ( ! popUp.trackDescriptionOnly )
                                            popUp.uiDialogOk($('#pop'), popUp.trackName);
                                        $(this).dialog("close");
                                }},
                                // popup.ready() doesn't seem to work in open.  So there is no need for open at this time.
                                //open: function() {
                                //     var subtrack = tdbIsSubtrack(hgTracks.trackDb[popUp.trackName]) ? popUp.trackName :"";  // If subtrack then vis rules differ
                                //     popUp.saveAllVars = getAllVars( $('#pop'), subtrack );
                                //},
                                open: function () {
                                    if ( ! popUp.trackDescriptionOnly ) {
                                        $('#hgTrackUiDialog').find('.filterBy,.filterComp').each(
                                            function(i) {
                                                if ($(this).hasClass('filterComp'))
                                                    ddcl.setup(this);
                                                else
                                                    ddcl.setup(this, 'noneIsAll');
                                            }
                                        );
                                    }
                                },
                                close: function() {
                                    popUp.cleanup();
                                }
                            });
        // FIXME: Why are open and close no longer working!!!
        if(popUp.trackDescriptionOnly) {
            var myWidth =  $(window).width() - 300;
            if(myWidth > 900)
                myWidth = 900;
            $('#hgTrackUiDialog').dialog("option", "maxWidth", myWidth);
            $('#hgTrackUiDialog').dialog("option", "width", myWidth);
            $('#hgTrackUiDialog').dialog('option' , 'title' ,
                               hgTracks.trackDb[popUp.trackName].shortLabel+" Track Description");
            $('#hgTrackUiDialog').dialog('open');
            var buttOk = $('button.ui-state-default');
            if($(buttOk).length == 1)
                $(buttOk).focus();
        } else {
            $('#hgTrackUiDialog').dialog('option' , 'title' ,
                                  hgTracks.trackDb[popUp.trackName].shortLabel+" Track Settings");
            $('#hgTrackUiDialog').dialog('open');
        }
    }
}

  ///////////////////////////////
 //// imageV2  (aka imgTbl) ////
///////////////////////////////
var imageV2 = {

    enabled:        false,    // Will be set to true unless advancedJavascriptFeatures is turned off OR if track search of config page
    imgTbl:         null,     // formerly "trackImgTbl"  The imgTbl or null if non-imageV2.
    inPlaceUpdate:  false,    // modified based on value of hgTracks.inPlaceUpdate and mapIsUpdateable
    mapIsUpdateable:true,
    lastTrack:      null,     // formerly (lastMapItem) this is used to try to keep what the last track the cursor passed.

    markAsDirtyPage: function ()
    {   // Page is marked as dirty so that the backbutton can be overridden
        var dirty = $('#dirty');
        if (dirty != undefined && dirty.length != 0)
            $(dirty).val('true');
    },

    isDirtyPage: function ()
    { // returns true if page was marked as dirty
    // This will allow the backbutton to be overridden

        var dirty = $('#dirty');
        if (dirty != undefined && dirty.length > 0) {
            if ($(dirty).val() == 'true')
                return true;
        }
        return false;
    },

    updateTiming: function (response)
    {   // update measureTiming text on current page based on what's in the response
        var reg = new RegExp("(<span class='timing'>.+?</span>)", "g");
        var strs = [];
        for(var a = reg.exec(response); a != null && a[1] != null; a = reg.exec(response)) {
            strs.push(a[1]);
        }
        if(strs.length > 0) {
            $('.timing').remove();
            for(var i = strs.length; i > 0; i--) {
                $('body').prepend(strs[i - 1]);
            }
        }
        reg = new RegExp("(<span class='trackTiming'>[\\S\\s]+?</span>)");
        a = reg.exec(response);
        if(a != null && a[1] != null) {
            $('.trackTiming').replaceWith(a[1]);
        }
    },

    loadSuggestBox: function ()
    {
        if($('#positionInput').length) {
            suggestBox.init(getDb(), $("#suggestTrack").length > 0,
                            function (item) {
                                genomePos.set(item.id, commify(getSizeFromCoordinates(item.id)));
                            },
                            function (position) {
                                genomePos.set(position, commify(getSizeFromCoordinates(position)));
                            });
            // Make sure suggestTrack is visible when user chooses something via gene select (#3484).
            if($("#suggestTrack").length) {
                $(document.TrackForm || document.TrackHeaderForm).submit(function(event) {
                                                       if($('#hgFindMatches').length) {
                                                           vis.makeTrackVisible($("#suggestTrack").val());
                                                       }
                                                   });
            }
        }
    },
    
    afterReload: function ()
    {   // Reload various UI widgets after updating imgTbl map.
        dragReorder.init();
        dragSelect.load(false);
        // Do NOT reload context menu (otherwise we get the "context menu sticks" problem).
        // rightClick.load($('#tr_' + id));
        if(imageV2.imgTbl.tableDnDUpdate)
            imageV2.imgTbl.tableDnDUpdate();
        rightClick.reloadFloatingItem();
        // Turn on drag scrolling.
        if(hgTracks.imgBoxPortal) {
            $("div.scroller").panImages();
        }
        imageV2.loadRemoteTracks();
        makeItemsByDrag.load();
        imageV2.markAsDirtyPage();
        imageV2.loadSuggestBox();
    },

    updateImgForId: function (html, id)
    {   // update row in imgTbl for given id.
        // return true if we successfully pull slice for id and update it in imgTrack.
        var str = "<TR id='tr_" + id + "'[^>]*>([\\s\\S]+?)</TR>";
        var reg = new RegExp(str);
        var a = reg.exec(html);
        if(a && a[1]) {
            var tr = $(document.getElementById("tr_" + id));
            if (tr.length > 0) {
                $(tr).html(a[1]);

                // Need to update tr class list too
                str = "<TR id='tr_" + id + "[^>]* class='(.*)'>";
                reg = new RegExp(str);
                var classes = reg.exec(html);
                if(classes && classes[1] && classes[1].length > 0) {
                    $(tr).removeClass();
                    $(tr).addClass(classes[1]);
                }

                // NOTE: Want to examine the png? Uncomment:
                //var img = $('#tr_' + id).find("img[id^='img_data_']").attr('src');
                //warn("Just parsed image:<BR>"+img);

                // >1x dragScrolling needs some extra care.
                if(hgTracks.imgBoxPortal && (hgTracks.imgBoxWidth > hgTracks.imgBoxPortalWidth)) {
                    if (hgTracks.imgBoxPortalLeft != undefined) {
                        $(tr).find('.panImg').css({'left': hgTracks.imgBoxPortalLeft });
                        $(tr).find('.tdData').css(
                                {'backgroundPosition': hgTracks.imgBoxPortalLeft});
                    }
                }
                return true;
            }
        }
        return false;
    },

    requestImgUpdate: function (trackName,extraData,loadingId,newVisibility)
    {
        // extraData, loadingId and newVisibility are optional
        var data = "hgt.trackImgOnly=1&hgsid=" + getHgsid() + "&hgt.trackNameFilter=" + trackName;
        if(extraData != undefined && extraData != "")
            data += "&" + extraData;
        if(loadingId == undefined || loadingId == "")
            loadingId = showLoadingImage("tr_" + trackName);
        var getOrPost = "GET";
        if ((data.length) > 2000) // extraData could contain a bunch of changes from the cfg dialog
            getOrPost = "POST";
        $.ajax({
                    type: getOrPost,
                    url: "../cgi-bin/hgTracks",
                    data: data,
                    dataType: "html",
                    trueSuccess: imageV2.updateImgAndMap,
                    success: catchErrorOrDispatch,
                    error: errorHandler,
                    cmd: 'refresh',
                    loadingId: loadingId,
                    id: trackName,
                    newVisibility: newVisibility,
                    cache: false
                });
    },

    fullReload: function()
    {
        // force reload of whole page via trackform submit
        // This function does not return
        jQuery('body').css('cursor', 'wait');
        document.TrackHeaderForm.submit();

    },

    updateImgAndMap: function (response, status)
    {   // Handle ajax response with an updated trackMap image, map and optional ideogram.
        //
        // this.cmd can be used to figure out which menu item triggered this.
        // this.id == appropriate track if we are retrieving just a single track.

        // update local hgTracks.trackDb to reflect possible side-effects of ajax request.
        var json = scrapeVariable(response, "hgTracks");
        var oldTrackDb = hgTracks.trackDb;
        var valid = false;
        if(json == undefined) {
            var stripped = new Object();
            stripJsEmbedded(response, true, stripped);
            if(stripped.warnMsg == null)
                warn("hgTracks object is missing from the response");
        } else {
            if(this.id != null) {
                if(json.trackDb[this.id]) {
                    var visibility = vis.enumOrder[json.trackDb[this.id].visibility];
                    var limitedVis;
                    if(json.trackDb[this.id].limitedVis)
                        limitedVis = vis.enumOrder[json.trackDb[this.id].limitedVis];
                    if(this.newVisibility && limitedVis && this.newVisibility != limitedVis)
                        // see redmine 1333#note-9
                        alert("There are too many items to display the track in " +
                                this.newVisibility + " mode.");
                    var rec = hgTracks.trackDb[this.id];
                    rec.limitedVis = json.trackDb[this.id].limitedVis;
                    vis.update(this.id, visibility);
                    valid = true;
                } else {
                    warn("Invalid hgTracks.trackDb received from the server");
                }
            } else {
                valid = true;
                hgTracks.trackDb = json.trackDb;
            }
        }
        if(valid) {
            if(imageV2.enabled
            && this.id
            && this.cmd
            && this.cmd != 'wholeImage'
            && this.cmd != 'selectWholeGene') {
                // Extract <TR id='tr_ID'>...</TR> and update appropriate row in imgTbl;
                // this updates src in img_left_ID, img_center_ID and img_data_ID and map in map_data_ID
                var id = this.id;
                if(imageV2.updateImgForId(response, id)) {
                    imageV2.afterReload();
                } else {
                    warn("Couldn't parse out new image for id: " + id);
                    //alert("Couldn't parse out new image for id: " + id+"BR"+response);  // Very helpful
                }
            } else {
                if(imageV2.enabled) {
                    // Implement in-place updating of hgTracks image
                    genomePos.setByCoordinates(json.chromName, json.winStart + 1, json.winEnd);
                    $("input[name='c']").val(json.chromName);
                    $("input[name='l']").val(json.winStart);
                    $("input[name='r']").val(json.winEnd);
                    if(json.cgiVersion != hgTracks.cgiVersion) {
                        // Must reload whole page because of a new version on the server; this should happen very rarely.
                        // Note that we have already updated position based on the user's action.
                        imageV2.fullReload();
                    } else {
                        // We update rows one at a time (b/c updating the whole imgTable at one time doesn't work in IE).
                        for (var id in hgTracks.trackDb) {
                        // handle case where invisible items may be in the trackDb list (see redmine #5670).
                            if(hgTracks.trackDb[id].type != "remote"
                            && hgTracks.trackDb[id].visibility > 0 // && $('#tr_' + id).length > 0
                            && !imageV2.updateImgForId(response, id)) {
                                warn("Couldn't parse out new image for id: " + id);
                            }
                        }
                    /* This (disabled) code handles dynamic addition of tracks:
                        for (var id in hgTracks.trackDb) {
                            if(oldTrackDb[id] == undefined) {
                                // XXXX Tim, what s/d abbr attribute be?
                                $('#imgTbl').append("<tr id='tr_" + id + "' class='imgOrd trDraggable'></tr>");
                                imageV2.updateImgForId(response, id);
                                vis.update(id, vis.enumOrder[hgTracks.trackDb[id].visibility]);
                            }
                        }
                    */
                        hgTracks = json;
                        genomePos.original = undefined;
                        initVars();
                        imageV2.afterReload();
                    }
                } else {
                    warn("ASSERT: Attempt to update track without advanced javascript features.");
                }
                // now pull out and parse the map.
                //a = /<MAP id='map' Name=map>([\s\S]+)<\/MAP>/.exec(response);
                //if(!a[1])
                //    warn("Couldn't parse out map");
            }
            // Parse out new ideoGram url (if available)
            // e.g.: <IMG SRC = "../trash/hgtIdeo/hgtIdeo_hgwdev_larrym_61d1_8b4a80.gif" BORDER=1 WIDTH=1039 HEIGHT=21 USEMAP=#ideoMap id='chrom'>
            // We do this last b/c it's least important.
            var a = /<IMG([^>]+SRC[^>]+id='chrom'[^>]*)>/.exec(response);
            if(a && a[1]) {
                var b = /SRC\s*=\s*"([^")]+)"/.exec(a[1]);
                if(b && b[1]) {
                    $('#chrom').attr('src', b[1]);
                }
            }
            if(hgTracks.measureTiming) {
                imageV2.updateTiming(response);
            }
        }
        if(this.disabledEle) {
            this.disabledEle.removeAttr('disabled');
        }
        if(this.loadingId) {
            hideLoadingImage(this.loadingId);
        }
        jQuery('body').css('cursor', '');
        if(valid && this.currentId) {
            var top = $(document.getElementById("tr_" + this.currentId)).position().top;
            $(window).scrollTop(top - this.currentIdYOffset);
        }
    },

    loadRemoteTracks: function ()
    {
        if(typeof(hgTracks.trackDb) != "undefined" && hgTracks.trackDb != null) {
            for (var id in hgTracks.trackDb) {
                var rec = hgTracks.trackDb[id];
                if(rec.type == "remote") {
                    if($("#img_data_" + id).length > 0) {
                        // load the remote track renderer via jsonp
                        rec.loadingId = showLoadingImage("tr_" + id);
                        var script = document.createElement('script');
                        var pos = parsePosition(genomePos.get());
                        var name = rec.remoteTrack || id;
                        script.setAttribute('src',
                                rec.url + "?track=" + name +
                                "&jsonp=imageV2.remoteTrackCallback&position=" +
                                encodeURIComponent(pos.chrom + ":" + pos.start + "-" + pos.end) +
                                "&pix=" + $('#imgTbl').width()
                        );
                        document.getElementsByTagName('head')[0].appendChild(script);
                    }
                }
            }
        }
    },

    remoteTrackCallback: function (rec)
    // jsonp callback to load a remote track.
    {
        if(rec.error) {
            alert("retrieval from remote site failed with error: " + rec.error)
        } else {
            var remoteTrack = rec.track;
            for (var track in hgTracks.trackDb) {
                if(hgTracks.trackDb[track].remoteTrack == remoteTrack) {
                    $('#img_data_' + track).attr('style', "left:-116px; top: -23px;");
                    $('#img_data_' + track).attr('height', rec.height);
                    // XXXX use width in some way?
           //       $('#img_data_' + track).attr('width', rec.width);
                    $('#img_data_' + track).attr('width', $('#img_data_ruler').width());
                    $('#img_data_' + track).attr('src', rec.img);
                    $('#td_data_' + track + ' > div').each(function(index) {
                        if(index == 1) {
                            var style = $(this).attr('style');
                            style = style.replace(/height:\s*\d+/i, "height:" + rec.height);
                            $(this).attr('style', style);
                        }
                    });
                    var style = $('#p_btn_' + track).attr('style');
                    style = style.replace(/height:\s*\d+/i, "height:" + rec.height);
                    $('#p_btn_' + track).attr('style', style);
                    if(hgTracks.trackDb[track].loadingId) {
                        hideLoadingImage(hgTracks.trackDb[track].loadingId);
                    }
                }
            }
        }
    },

    navigateButtonClick: function (ele) // called from hgTracks.c
    {   // code to update just the imgTbl in response to navigation buttons (zoom-out etc.).
        if(imageV2.inPlaceUpdate) {
            var params = ele.name + "=" + ele.value;
            $(ele).attr('disabled', 'disabled');
            // dinking navigation needs additional data
            if(ele.name == "hgt.dinkLL" || ele.name == "hgt.dinkLR") {
                params += "&dinkL=" + $("input[name='dinkL']").val();
            } else if(ele.name == "hgt.dinkRL" || ele.name == "hgt.dinkRR") {
                params += "&dinkR=" + $("input[name='dinkR']").val();
            }
            imageV2.navigateInPlace(params, $(ele), false);
            return false;
        } else {
            return true;
        }
    },

    updateButtonClick: function (ele) // UNUSED?
    {   // code to update the imgTbl based on changes in the track controls.
        // This is currently experimental code and is dead in the main branch.
        if(imageV2.mapIsUpdateable) {
            var data = "";
            $("select").each(function(index, o) {
                var cmd = $(this).val();
                if(cmd == "hide") {
                    if(hgTracks.trackDb[this.name] != undefined) {
                        alert("Need to implement hide");
                    }
                } else {
                    if(hgTracks.trackDb[this.name] == undefined
                    || cmd != vis.enumOrder[hgTracks.trackDb[this.name].visibility]) {
                        if(data.length > 0) {
                            data = data + "&";
                        }
                        data = data + this.name + "=" + cmd;
                    }
            }
            });
            if(data.length > 0) {
                imageV2.navigateInPlace(data, null, false);
            }
            return false;
        } else {
            return true;
        }
    },

    navigateInPlace: function (params, disabledEle, keepCurrentTrackVisible)
    {
    // request an hgTracks image, using params
    // disabledEle is optional; this element will be enabled when update is complete
    // If keepCurrentTrackVisible is true, we try to maintain relative position of the item under the mouse after the in-place update.
        // Tim thinks we should consider disabling all UI input while we are doing in-place update.
        jQuery('body').css('cursor', 'wait');
        var currentId, currentIdYOffset;
        if(keepCurrentTrackVisible) {
            var item = rightClick.currentMapItem || imageV2.lastTrack;
            if(item) {
                var top = $(document.getElementById("tr_" + item.id)).position().top;
                if(top >= $(window).scrollTop()
                || top < $(window).scrollTop() + $(window).height()) {
                    // don't bother if the item is not currently visible.
                    currentId = item.id;
                    currentIdYOffset = top - $(window).scrollTop();
                }
            }
        }
        $.ajax({
                type: "GET",
                url: "../cgi-bin/hgTracks",
                data: params + "&hgt.trackImgOnly=1&hgt.ideogramToo=1&hgsid=" + getHgsid(),
                dataType: "html",
                trueSuccess: imageV2.updateImgAndMap,
                success: catchErrorOrDispatch,
                error: errorHandler,
                cmd: 'wholeImage',
                loadingId: showLoadingImage("imgTbl"),
                disabledEle: disabledEle,
                currentId: currentId,
                currentIdYOffset: currentIdYOffset,
                cache: false
            });
    }

}

  //////////////////////
 //// track search ////
//////////////////////
var trackSearch = {

    searchKeydown: function (event)
    {
        if (event.which == 13) {
            // Required to fix problem on IE and Safari where value of hgt_tSearch is "-" (i.e. not "Search").
            $("input[name=hgt_tsPage]").val(0);  // NOTE: must match TRACK_SEARCH_PAGER in hg/inc/searchTracks.h
            $('#trackSearch').submit();
            // This doesn't work with IE or Safari.
            // $('#searchSubmit').click();
        }
    },

    init: function ()
    {
        // Track search uses tabs
        if($("#tabs").length > 0) {
            // Search page specific code

            var val = $('#currentTab').val();
            $("#tabs").tabs({
                                show: function(event, ui) {
                                    $('#currentTab').val(ui.panel.id);
                                },
                                select: function(event, ui) { findTracks.switchTabs(ui); }
                            });
            $('#tabs').show();
            $("#tabs").tabs('option', 'selected', '#' + val);
            if(val =='simpleTab' && $('div#found').length < 1) {
                $('input#simpleSearch').focus();
            }
            $("#tabs").css('font-family', jQuery('body').css('font-family'));
            $("#tabs").css('font-size', jQuery('body').css('font-size'));
            $('.submitOnEnter').keydown(trackSearch.searchKeydown);
            findTracks.normalize();
            findTracks.updateMdbHelp(0);
        }
    }
}


  ///////////////
 //// READY ////
///////////////
$(document).ready(function()
{
    // The page may be reached via browser history (back button)
    // If so, then this code should detect if the image has been changed via js/ajax
    // and will reload the image if necessary.
    // NOTE: this is needed for IE but other browsers can detect the dirty page much earlier
    if (imageV2.isDirtyPage()) {
        // mark as non dirty to avoid infinite loop in chrome.
        $('#dirty').val('false');
        jQuery('body').css('cursor', 'wait');
            window.location = "../cgi-bin/hgTracks?hgsid=" + getHgsid();
            return false;
    }
    initVars();
    imageV2.loadSuggestBox();
    // Convert map AREA gets to post the form, ensuring that cart variables are kept up to date (but turn this off for search form).
    if($("FORM").length > 0 && $('#trackSearch').length == 0) {
        var allLinks = $('a');
        $( allLinks ).unbind('click');
        $( allLinks ).click( posting.saveSettings );
    }
    if($('#pdfLink').length == 1) {
        $('#pdfLink').click(function(i) {
            var thisForm=$('#TrackForm');
            if(thisForm != undefined && $(thisForm).length == 1) {
                //alert("posting form:"+$(thisForm).attr('name'));
                updateOrMakeNamedVariable($(thisForm),'hgt.psOutput','on');
                return postTheForm($(thisForm).attr('name'),this.href);
            }
            return true;
        });
    }

    if(imageV2.enabled) {

        // Make imgTbl allow drag reorder of imgTrack rows
        dragReorder.init();
        var imgTable = $(".tableWithDragAndDrop");
        if($(imgTable).length > 0) {
            $(imgTable).tableDnD({
                onDragClass: "trDrag",
                dragHandle: "dragHandle",
                scrollAmount: 40,
                onDragStart: function(ev, table, row) {
                    mouse.saveOffset(ev);
                    $(document).bind('mousemove',posting.blockTheMapOnMouseMove);

                    // Can drag a contiguous set of rows if dragging blue button
                    table.tableDnDConfig.dragObjects = [ row ]; // defaults to just the one
                    var btn = $( row ).find('p.btnBlue');  // btnBlue means cursor over left button
                    if (btn.length == 1) {
                        table.tableDnDConfig.dragObjects = dragReorder.getContiguousRowSet(row);
                        var compositeSet = dragReorder.getCompositeSet(row);
                        if (compositeSet && compositeSet.length > 0)
                            $( compositeSet ).find('p.btn').addClass('blueButtons');  // blue persists
                    }
                },
                onDrop: function(table, row, dragStartIndex) {
                    var compositeSet = dragReorder.getCompositeSet(row);
                    if (compositeSet && compositeSet.length > 0)
                        $( compositeSet ).find('p.btn').removeClass('blueButtons');  // blue persists
                    if($(row).attr('rowIndex') != dragStartIndex) {
                        // NOTE Even if dragging a contiguous set of rows,
                        // still only need to check the one under the cursor.
                        if(dragReorder.setOrder) {
                            dragReorder.setOrder(table);
                        }
                        dragReorder.zipButtons( table );
                    }
                    $(document).unbind('mousemove',posting.blockTheMapOnMouseMove);
                    setTimeout('posting.allowMapClicks();',50); // Necessary incase the onDrop was over a map item. onDrop takes precedence.
                }
            });
        }

        // Drag scroll init
        if(hgTracks.imgBoxPortal) {
            // Turn on drag scrolling.
            $("div.scroller").panImages();
        }
        //$("#zoomSlider").slider({ min: -4, max: 3, step: 1 });//, handle: '.ui-slider-handle' });

        // Retrieve tracks via AJAX that may take too long to draw initialliy (i.e. a remote bigWig)
        var retrievables = $('#imgTbl').find("tr.mustRetrieve")
        if($(retrievables).length > 0) {
            $(retrievables).each( function (i) {
                var trackName = $(this).attr('id').substring(3);
                imageV2.requestImgUpdate(trackName,"","");
            });
        }
        imageV2.loadRemoteTracks();
        makeItemsByDrag.load();
    }

    // Drag select in chromIdeogram
    if($('img#chrom').length == 1) {
        if($('area.cytoBand').length > 1) {
            $('img#chrom').chromDrag();
        }
    }

    // Track search uses tabs
    trackSearch.init();

    // Drag select initialize
    if (imageV2.enabled) {   // moved from window.load().
        dragSelect.load(true);

        if($('#hgTrackUiDialog'))
            $('#hgTrackUiDialog').hide();

        // Don't load contextMenu if jquery.contextmenu.js hasn't been loaded
        if (jQuery.fn.contextMenu) {
            rightClick.load(imageV2.imgTbl);
        }
    }
});
