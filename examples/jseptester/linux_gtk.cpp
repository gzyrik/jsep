#include <unistd.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include "../jsep/webrtc.h"
#include "../zmf/zmf.h"
#include <string.h>
#include <memory>
#include <map>
enum {
    BUTTON_CONNECT,
    BUTTON_CALL,
    BUTTON_SEND,
    BUTTON_CLEAR_LOG,
    BUTTON_GET_STATS,
    BUTTON_LOG_EVENT,
    BUTTON_COUNT,
};
static gboolean ReDraw(gpointer data){
    gtk_widget_queue_draw((GtkWidget*)data);
    return false;
}
struct VideoRenderer {
    const std::string  id_;
    std::unique_ptr<uint8_t[]> image_;
    int width_;
    int height_;
    GtkWidget* draw_area_;
    int OnFrame(int* iWidth, int* iHeight, unsigned char *buf) {
        gdk_threads_enter();
        if (width_ != *iWidth || height_ != *iHeight) {
            width_ = *iWidth;
            height_ = *iHeight;
            image_.reset(new uint8_t[width_ * height_ * 4]);
        }
        Zmf_ConvertFromI420(image_.get(), ZmfPixelFormatARGB, buf, width_, height_);
        gdk_threads_leave();
        g_idle_add(ReDraw, draw_area_);
        return 1;
    }
    static int zmfRenderCallback(void* pUser, const char* renderId, int sourceType, int iAngle,
        int iMirror, int* iWidth, int* iHeight, unsigned char *buf, unsigned long timeStamp) {
        VideoRenderer* render = (VideoRenderer*)pUser;
        if (!buf || !iWidth || !iHeight || render->id_ != renderId) return 0;
        return render->OnFrame(iWidth, iHeight, buf);
    }
    VideoRenderer(GtkWidget* draw_area, const std::string& streamId):
        id_(streamId), width_(0), height_(0), draw_area_(draw_area){
            Zmf_VideoRenderAddCallback(this, zmfRenderCallback);
        }
    ~VideoRenderer(){
        Zmf_VideoRenderRemoveCallback(this);
    }
};
#define BUTTON_WIDTH    70
#define EDIT_WIDTH      30
#define VIDEO_WIDTH     640
#define VIDEO_HEIGHT    480
#define PAD_WIDTH       (10+EDIT_WIDTH+BUTTON_WIDTH)
#define WND_WIDTH       (PAD_WIDTH+VIDEO_WIDTH)
struct GtkMainWnd : public WebRTC::View
{
    GtkWidget* window_;
    GtkWidget* ws_edit_, *id_edit_;
    GtkWidget* stun_edit_, *to_edit_;
    GtkWidget* msg_edit_;
    GtkWidget* log_list_;
    GtkWidget* draw_area_;
    GtkWidget* buttons_[BUTTON_COUNT];
    std::unique_ptr<VideoRenderer> local_renderer_;
    std::unique_ptr<VideoRenderer> remote_video_, remote_share_;
    std::map<std::string,std::string> m_streams;//streamId->type
    std::string m_statsType;

    WebRTC* m_client;
    bool m_calling;
    std::string m_peershare, m_peervideo;
    GtkMainWnd():m_client(0),m_calling(false){}
    void Create();
    void OnRedraw();
    void OnQuit();
    void OnKeyPress(GdkEventKey* key){}
    void Draw(GtkWidget* widget, cairo_t* cr);
public:
    void OnBnClickedConnect();
    void OnBnClickedCall();
    void OnBnClickedSend();
    void OnBnClickedClear();
    void OnBnClickedGetStats();
    void OnBnClickedLogEvent();
public:
    virtual void OnOpen();
    virtual void OnClose();
    virtual void OnCall(const std::string& fromId, bool p2pOnly);
    virtual void OnHangup();
    virtual void OnMessage(const std::string& message, const std::string& fromId);
    virtual void OnAddStream(const std::string& streamId, const std::string& type);
    virtual void OnRemoveStream(const std::string& streamId, const std::string& type);
    virtual void Trace(const char* format, ...);
    virtual void OnStatsReport(const std::string& statsTyep, const std::string& statsId, const std::string& stats, const double timestamp);
    virtual void _QueueUIThreadCallback(int msg_id, char* data);
};
static GtkMainWnd _wnd;
static gboolean Draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    _wnd.Draw(widget, cr);
    return false;
}
static gboolean OnDestroyedCallback(GtkWidget* widget, GdkEvent* event, gpointer data) {
    _wnd.OnQuit();
    return false;
}
static gboolean OnKeyPressCallback(GtkWidget* widget, GdkEventKey* key, gpointer data) {
    _wnd.OnKeyPress(key);
    return false;
}
void OnClickedCallback(GtkWidget* widget, gpointer data) {
    const int button = GPOINTER_TO_INT(data);
    switch(button) {
    case BUTTON_CONNECT:
        return _wnd.OnBnClickedConnect();
    case BUTTON_CALL:
        return _wnd.OnBnClickedCall();
    case BUTTON_SEND:
        return _wnd.OnBnClickedSend();
    case BUTTON_CLEAR_LOG:
        return _wnd.OnBnClickedClear();
    case BUTTON_GET_STATS:
        return _wnd.OnBnClickedGetStats();
    case BUTTON_LOG_EVENT:
        return _wnd.OnBnClickedLogEvent();
    }
}
void GtkMainWnd::Create() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    gtk_widget_set_size_request(window_, WND_WIDTH, VIDEO_HEIGHT);
    gtk_container_set_border_width(GTK_CONTAINER(window_), 0);
    gtk_window_set_resizable(GTK_WINDOW(window_), false);
    gtk_window_set_title(GTK_WINDOW(window_), "Jsep Tester");
    g_signal_connect(window_, "delete-event", G_CALLBACK(&OnDestroyedCallback), 0);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(OnKeyPressCallback), 0);

    GtkWidget* pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(pad, PAD_WIDTH, VIDEO_HEIGHT);
    {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new("WS:"));
        ws_edit_ = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(ws_edit_), "ws://192.168.0.240:7000");
        gtk_box_pack_start(GTK_BOX(hbox), ws_edit_, true, true, 0);

        gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new("ID:"));
        id_edit_ = gtk_entry_new();
        gtk_widget_set_size_request(id_edit_, EDIT_WIDTH, -1);
        gtk_container_add(GTK_CONTAINER(hbox), id_edit_);

        buttons_[BUTTON_CONNECT] = gtk_button_new_with_label("Connect");
        gtk_widget_set_size_request(buttons_[BUTTON_CONNECT], BUTTON_WIDTH, -1);
        g_signal_connect(buttons_[BUTTON_CONNECT], "clicked", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_CONNECT);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_CONNECT]);

        gtk_container_add(GTK_CONTAINER(pad), hbox);
    }
    {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new("STUN:"));
        stun_edit_ = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(stun_edit_), "stun:test@115.29.4.150:3478");
        gtk_box_pack_start(GTK_BOX(hbox), stun_edit_, true, true, 0);

        gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new("TO:"));
        to_edit_ = gtk_entry_new();
        gtk_widget_set_size_request(to_edit_, EDIT_WIDTH, -1);
        gtk_container_add(GTK_CONTAINER(hbox), to_edit_);

        buttons_[BUTTON_CALL] = gtk_button_new_with_label("Call");
        gtk_widget_set_size_request(buttons_[BUTTON_CALL], BUTTON_WIDTH, -1);
        gtk_widget_set_sensitive(buttons_[BUTTON_CALL], false);
        g_signal_connect(buttons_[BUTTON_CALL], "clicked", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_CALL);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_CALL]);

        gtk_container_add(GTK_CONTAINER(pad), hbox);
    }
    {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        gtk_container_add(GTK_CONTAINER(hbox), gtk_label_new("MSG:"));
        msg_edit_ = gtk_entry_new();
        gtk_box_pack_start(GTK_BOX(hbox), msg_edit_, true, true, 0);

        buttons_[BUTTON_SEND] = gtk_button_new_with_label("Send");
        gtk_widget_set_size_request(buttons_[BUTTON_SEND], BUTTON_WIDTH, -1);
        gtk_widget_set_sensitive(buttons_[BUTTON_SEND], false);
        g_signal_connect(buttons_[BUTTON_SEND], "clicked", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_SEND);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_SEND]);

        gtk_container_add(GTK_CONTAINER(pad), hbox);
    }
    {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        buttons_[BUTTON_CLEAR_LOG] = gtk_button_new_with_label("Clear Log");
        g_signal_connect(buttons_[BUTTON_CLEAR_LOG], "clicked", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_CLEAR_LOG);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_CLEAR_LOG]);

        buttons_[BUTTON_GET_STATS] = gtk_button_new_with_label("Get Stats");
        gtk_widget_set_sensitive(buttons_[BUTTON_GET_STATS], false);
        g_signal_connect(buttons_[BUTTON_GET_STATS], "clicked", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_GET_STATS);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_GET_STATS]);

        buttons_[BUTTON_LOG_EVENT] = gtk_check_button_new_with_label("Log Event");
        gtk_widget_set_sensitive(buttons_[BUTTON_LOG_EVENT], false);
        g_signal_connect(buttons_[BUTTON_LOG_EVENT], "toggled", G_CALLBACK(OnClickedCallback), (gpointer)BUTTON_LOG_EVENT);
        gtk_container_add(GTK_CONTAINER(hbox), buttons_[BUTTON_LOG_EVENT]);

        gtk_container_add(GTK_CONTAINER(pad), hbox);
    }
    {
        GtkWidget *scroll = gtk_scrolled_window_new(0,0);
        log_list_ = gtk_tree_view_new();
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(log_list_), false);
        gtk_container_add(GTK_CONTAINER(scroll), log_list_);
        gtk_box_pack_end(GTK_BOX(pad), scroll, true, true, 0);
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes("Title", renderer, "text", 0, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(log_list_), column);
        GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
        gtk_tree_view_set_model(GTK_TREE_VIEW(log_list_), GTK_TREE_MODEL(store));
        g_object_unref(store);
    }
    GtkWidget* main = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main), pad, true, true, 0);
    draw_area_ = gtk_drawing_area_new();
    gtk_widget_set_size_request(draw_area_, VIDEO_WIDTH, VIDEO_HEIGHT);
    g_signal_connect(G_OBJECT(draw_area_), "draw", G_CALLBACK(&::Draw), this);
    gtk_container_add(GTK_CONTAINER(main), draw_area_);
    gtk_container_add(GTK_CONTAINER(window_), main);
    gtk_widget_show_all(window_);
}
void GtkMainWnd::Draw(GtkWidget* widget, cairo_t* cr) {
    cairo_surface_t* image;
    if (!remote_share_ && !remote_video_) {
        cairo_set_source_rgba (cr, 0.9, 0.9, 0.9, 1.0);
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint (cr);
        return;
    }
    gdk_threads_enter();
    if (remote_share_ && remote_share_->image_.get()) {
        image = cairo_image_surface_create_for_data(remote_share_->image_.get(),
            CAIRO_FORMAT_RGB24, remote_share_->width_, remote_share_->height_,
            cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, remote_share_->width_));

        cairo_rectangle(cr, 0, 0, remote_share_->width_, remote_share_->height_);
        cairo_set_source_surface (cr, image, 0, 0);
        cairo_fill (cr);
        cairo_surface_destroy(image);
    }
    if (remote_video_ && remote_video_->image_.get()){
        image = cairo_image_surface_create_for_data(remote_video_->image_.get(),
            CAIRO_FORMAT_RGB24, remote_video_->width_, remote_video_->height_,
            cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, remote_video_->width_));
        cairo_rectangle(cr, 0, 0, remote_video_->width_, remote_video_->height_);
        if (remote_share_ && remote_share_->image_.get())
            cairo_scale (cr, 0.25, 0.25);
        cairo_set_source_surface (cr, image, 0, 0);
        cairo_fill (cr);
        cairo_surface_destroy(image);
    }
    gdk_threads_leave();
}
void GtkMainWnd::OnQuit(){
    OnClose();
    gtk_widget_destroy(window_);
    remote_video_.reset();
    local_renderer_.reset();
    Zmf_AudioTerminate();
    Zmf_VideoTerminate();
    gtk_main_quit();
}
void GtkMainWnd::OnBnClickedConnect() {
    if (m_client) return OnClose();
    const char* ws = gtk_entry_get_text(GTK_ENTRY(ws_edit_));
    const char* id = gtk_entry_get_text(GTK_ENTRY(id_edit_));
    if (strlen(ws) == 0 || strlen(id) == 0)
        return Trace("invalid WS or ID");

    m_client = WebRTC::Connect(*this, ws, id);
    if (!m_client) return Trace("Connect failed!");
    gtk_widget_set_sensitive(buttons_[BUTTON_CONNECT], false);
    gtk_widget_set_sensitive(ws_edit_, false);
    gtk_widget_set_sensitive(id_edit_, false);
}
void GtkMainWnd::OnBnClickedCall() {
    if (m_calling)
        OnHangup();
    else{
        const char* toId = gtk_entry_get_text(GTK_ENTRY(to_edit_));
        if (strlen(toId) == 0)
            return Trace("invalid TO");
        OnCall(toId, false);
    }
}
void GtkMainWnd::OnBnClickedSend() {
    if (!m_client) return;
    const char* msg = gtk_entry_get_text(GTK_ENTRY(msg_edit_));
    const char* to = gtk_entry_get_text(GTK_ENTRY(to_edit_));
    if (strlen(msg) == 0 || strlen(to) == 0)
        return Trace("invalid MSG or TO");
    m_client->Send(msg, to);
}
void GtkMainWnd::OnBnClickedClear() {
    GtkListStore* store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(log_list_)));
    gtk_list_store_clear(store);
}
void GtkMainWnd::OnStatsReport(const std::string& statsTyep, const std::string& statsId, const std::string& stats, const double timestamp) {
    Trace("%s: %s:%s=%s", m_statsType.c_str(), statsTyep.c_str(), statsId.c_str(), stats.c_str());
}
void GtkMainWnd::OnBnClickedGetStats() {
    if (m_client) {
        static size_t statsCount = 0;
        if (statsCount < m_streams.size()) {
            std::map<std::string, std::string>::iterator iter = m_streams.begin();
            for (size_t i = 0; i < statsCount; ++i) iter++;
            m_client->GetStats(iter->first, WebRTC::Audio | WebRTC::Video);
            m_statsType = iter->second;
            statsCount++;
        }
        else {
            m_client->GetStats("", WebRTC::Audio | WebRTC::Video);
            m_statsType = "STATS";
            statsCount = 0;
        }  
    }
}
void GtkMainWnd::OnBnClickedLogEvent() {
    bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttons_[BUTTON_LOG_EVENT]));
    if (m_client) m_client->LogRtcEvent(active ? "event.log":"");
}
void GtkMainWnd::OnOpen() {
    gtk_button_set_label(GTK_BUTTON(buttons_[BUTTON_CONNECT]), "Disconnect");
    gtk_widget_set_sensitive(buttons_[BUTTON_CONNECT], true);
    gtk_widget_set_sensitive(buttons_[BUTTON_SEND], true);
    gtk_widget_set_sensitive(buttons_[BUTTON_CALL], true);
}
void GtkMainWnd::OnClose() {
    OnHangup();
    if (m_client) m_client->Close();
    m_client = 0; 
    gtk_widget_set_sensitive(buttons_[BUTTON_CALL], false);
    gtk_widget_set_sensitive(buttons_[BUTTON_SEND], false);
    gtk_widget_set_sensitive(ws_edit_, true);
    gtk_widget_set_sensitive(id_edit_, true);
    gtk_button_set_label(GTK_BUTTON(buttons_[BUTTON_CONNECT]), "Connect");
    gtk_widget_set_sensitive(buttons_[BUTTON_CONNECT], true);
    Trace("--- close ok ---");
}
void GtkMainWnd::OnCall(const std::string& fromId, bool p2pOnly) {
    if (m_calling) return;

    const char* url = gtk_entry_get_text(GTK_ENTRY(stun_edit_));
    if (strlen(url) == 0)
        return Trace("invalid STUN");
    m_peershare.clear();
    m_peervideo.clear();
    if (!m_client->Call(url, "", true, fromId))
        return;

    gtk_entry_set_text(GTK_ENTRY(to_edit_), fromId.c_str());
    gtk_widget_set_sensitive(buttons_[BUTTON_GET_STATS], true);
    gtk_widget_set_sensitive(buttons_[BUTTON_LOG_EVENT], true);
    gtk_widget_set_sensitive(stun_edit_, false);
    gtk_widget_set_sensitive(to_edit_, false);
    gtk_button_set_label(GTK_BUTTON(buttons_[BUTTON_CALL]), "Hangup");

    //Zmf_VideoRenderStart((void*)m_viewVideo.GetSafeHwnd(), ZmfRenderView);
    m_calling = true;
    Trace("--- call ok ---");
}
void GtkMainWnd::OnHangup(){
    if (!m_calling) return;
    m_calling = false;
    if (m_client) m_client->Hangup();

    gtk_widget_set_sensitive(buttons_[BUTTON_GET_STATS], false);
    gtk_widget_set_sensitive(buttons_[BUTTON_LOG_EVENT], false);
    //Zmf_VideoRenderStop((void*)m_viewVideo.GetSafeHwnd());
    //Zmf_VideoRenderRemoveAll((void*)m_viewVideo.GetSafeHwnd());
    gtk_widget_set_sensitive(stun_edit_, true);
    gtk_widget_set_sensitive(to_edit_, true);
    gtk_button_set_label(GTK_BUTTON(buttons_[BUTTON_CALL]), "Call");
    //Zmf_VideoRenderStop((void*)m_viewVideo.GetSafeHwnd());
    //Zmf_VideoRenderRemoveAll((void*)m_viewVideo.GetSafeHwnd());
    remote_video_.reset();
    remote_share_.reset();
    gtk_widget_queue_draw(draw_area_);
    Trace("--- hangup ok ---");
}
void GtkMainWnd::OnMessage(const std::string& message, const std::string& fromId) {
    Trace("%s>>%s", fromId.c_str(), message.c_str());
}
void GtkMainWnd::OnAddStream(const std::string& streamId, const std::string& type) {
    if (type != "previewvideo" && type != "previewshare")
        m_streams[streamId] = type;
    Trace("--- add %s: %s", type.c_str(), streamId.c_str());
    if (type == "peervideo") {
        remote_video_.reset(new VideoRenderer(draw_area_, streamId));
        char constraints[1024];
        sprintf(constraints, "{\"video\":{\"zmfRender\":\"%s\"}}", streamId.c_str());
        m_client->Update(std::string(constraints));
    }
    else if (type == "peershare"){
        remote_share_.reset(new VideoRenderer(draw_area_, streamId));
    }
}
void GtkMainWnd::OnRemoveStream(const std::string& streamId, const std::string& type) {
    if (type != "previewvideo" && type != "previewshare")
        m_streams.erase(streamId);
    Trace("--- remove %s: %s", type.c_str(), streamId.c_str());
    if (type == "peervideo")
        remote_video_.reset();
    else if (type == "peershare")
        remote_share_.reset();
}
void GtkMainWnd::Trace(const char* format, ...) {
    char log[4096];
    va_list args;
    va_start(args, format);
    vsprintf(log, format, args);
    va_end(args);

    GtkListStore* store = GTK_LIST_STORE( gtk_tree_view_get_model(GTK_TREE_VIEW(log_list_)));
    GtkTreeIter iter;
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, log, -1);
}
struct UIThreadCallbackData {
    explicit UIThreadCallbackData(int id, char* d) : msg_id(id), data(d) {}
    int msg_id;
    char* data;
};
static gboolean HandleUIThreadCallback(gpointer data) {
    UIThreadCallbackData* cb_data = reinterpret_cast<UIThreadCallbackData*>(data);
    _wnd.m_client->_UIThreadCallback(cb_data->msg_id, cb_data->data);
    delete cb_data;
    return false;
}
void GtkMainWnd::_QueueUIThreadCallback(int msg_id, char* data) {
    g_idle_add(HandleUIThreadCallback, new UIThreadCallbackData(msg_id, data));
}
int main(int argc, char* argv[])
{
    gtk_init(&argc, &argv);
    _wnd.Create();
    gtk_main();
    return 0;
}
