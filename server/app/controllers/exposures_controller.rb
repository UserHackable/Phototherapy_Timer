class ExposuresController < ApplicationController
  before_action :set_user
  before_action :set_exposure, only: %i[ show edit update destroy ]

  # GET /users/:user_id/exposures
  def index
    @exposures = @user.exposures.includes(:therapy_type, :skin_type).newest_first
  end

  # GET /users/:user_id/exposures/:id
  def show
  end

  # GET /users/:user_id/exposures/new
  def new
    @exposure = @user.exposures.new(started_at: Time.current, duration_seconds: 60)
  end

  # GET /users/:user_id/exposures/:id/edit
  def edit
  end

  # POST /users/:user_id/exposures
  def create
    @exposure = @user.exposures.new(exposure_params)

    if @exposure.save
      redirect_to [ @user, @exposure ], notice: "Exposure was successfully created."
    else
      render :new, status: :unprocessable_content
    end
  end

  # PATCH/PUT /users/:user_id/exposures/:id
  def update
    if @exposure.update(exposure_params)
      redirect_to [ @user, @exposure ], notice: "Exposure was successfully updated.", status: :see_other
    else
      render :edit, status: :unprocessable_content
    end
  end

  # DELETE /users/:user_id/exposures/:id
  def destroy
    @exposure.destroy!
    redirect_to user_exposures_path(@user), notice: "Exposure was successfully destroyed.", status: :see_other
  end

  private
    def set_user
      @user = User.find(params.expect(:user_id))
    end

    def set_exposure
      @exposure = @user.exposures.find(params.expect(:id))
    end

    def exposure_params
      permitted = params.expect(exposure: [ :started_at, :duration_seconds, :therapy_type_id, :skin_type_id ])
      permitted[:therapy_type_id] = nil if permitted[:therapy_type_id].blank?
      permitted[:skin_type_id] = nil if permitted[:skin_type_id].blank?
      permitted
    end
end
